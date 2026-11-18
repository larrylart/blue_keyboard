////////////////////////////////////////////////////////////////////
// bkproto.js - Blue Keyboard client protocol for the browser
//
// Transport-agnostic implementation of the dongle's binary protocol:
//   - Framing:      [OP][LEN_le16][PAYLOAD]
//   - Provisioning: A0 -> A2 (challenge) -> A3 (proof) -> A1 (wrapped AppKey)
//   - MTLS:         B0 (hello) -> B1 (keyx) -> B2 (sfin), then B3 records
//   - App ops:      C1 (info) -> C2, D0 (type string) -> D1
//
// Mirrors blue_keyboard/mtls.cpp and blue_keyboard/commands.h.
// Uses only WebCrypto (crypto.subtle), so it needs a secure context
// (HTTPS). The AppKey is held as a non-extractable HMAC CryptoKey: every
// use of it in the protocol is an HMAC (KEYX MAC and the HKDF-Extract
// step), so the raw bytes never need to be read back after import.
////////////////////////////////////////////////////////////////////

export const SERVICE_UUID     = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
export const CHAR_WRITE_UUID  = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // phone -> dongle
export const CHAR_NOTIFY_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // dongle -> phone

// Largest UTF-8 text we send in one D0. A whole frame must fit in a single
// GATT write (attribute max 512 bytes): 3 (B3 hdr) + 4 (seq/clen) + 3 (D0 hdr) + 16 (mac).
export const MAX_TEXT_BYTES = 480;

const subtle = globalThis.crypto.subtle;
const utf8 = new TextEncoder();
const ascii = (s) => utf8.encode(s);

export function concat(...parts)
{
	let n = 0;
	for( const p of parts ) n += p.length;
	const out = new Uint8Array(n);
	let o = 0;
	for( const p of parts ) { out.set(p, o); o += p.length; }
	return out;
}

const be16 = (v) => new Uint8Array([(v >> 8) & 0xFF, v & 0xFF]);
const rdBe16 = (b, o) => (b[o] << 8) | b[o + 1];

export function frame(op, payload = new Uint8Array(0))
{
	const n = payload.length;
	return concat(new Uint8Array([op, n & 0xFF, (n >> 8) & 0xFF]), payload);
}

function equal(a, b)
{
	if( a.length !== b.length ) return false;
	let r = 0;
	for( let i = 0; i < a.length; i++ ) r |= a[i] ^ b[i];
	return r === 0;
}

const errText = (payload) => new TextDecoder().decode(payload) || 'device error';

////////////////////////////////////////////////////////////////////
// Crypto helpers (WebCrypto)
////////////////////////////////////////////////////////////////////
export async function importHmacKey(raw)
{
	return subtle.importKey('raw', raw, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
}

async function hmac(key, data)
{
	if( !(key instanceof CryptoKey) ) key = await importHmacKey(key);
	return new Uint8Array(await subtle.sign('HMAC', key, data));
}

const hmac16 = async (key, data) => (await hmac(key, data)).slice(0, 16);

async function aesCtr(key, iv16, data)
{
	if( !(key instanceof CryptoKey) )
		key = await subtle.importKey('raw', key, 'AES-CTR', false, ['encrypt']);
	// length 128: the whole IV block is the counter, like mbedtls_aes_crypt_ctr
	return new Uint8Array(await subtle.encrypt({ name: 'AES-CTR', counter: iv16, length: 128 }, key, data));
}

async function pbkdf2(password, salt, iterations)
{
	const base = await subtle.importKey('raw', utf8.encode(password), 'PBKDF2', false, ['deriveBits']);
	return new Uint8Array(await subtle.deriveBits({ name: 'PBKDF2', hash: 'SHA-256', salt, iterations }, base, 256));
}

////////////////////////////////////////////////////////////////////
// Framer - reassembles notification chunks into frames.
// Same sanity limit / resync behaviour as the iOS client (Framer.swift).
////////////////////////////////////////////////////////////////////
export class Framer
{
	constructor() { this.buf = new Uint8Array(0); }

	push(chunk)
	{
		this.buf = concat(this.buf, chunk);
		const out = [];
		for(;;)
		{
			if( this.buf.length < 3 ) break;
			const len = this.buf[1] | (this.buf[2] << 8);
			if( len > 1024 )
			{
				// garbage: drop up to the next plausible binary opcode
				let pos = 1;
				while( pos < this.buf.length && this.buf[pos] < 0x80 ) pos++;
				this.buf = this.buf.slice(pos);
				continue;
			}
			if( this.buf.length < 3 + len ) break;
			out.push({ op: this.buf[0], payload: this.buf.slice(3, 3 + len) });
			this.buf = this.buf.slice(3 + len);
		}
		return out;
	}
}

////////////////////////////////////////////////////////////////////
// BkClient - one connection's protocol state.
//   write(bytes): Promise  - sends one complete frame (one GATT write)
//   feed(chunk)            - call with every notification value
////////////////////////////////////////////////////////////////////
export class BkClient
{
	constructor(write)
	{
		this.write = write;
		this.framer = new Framer();
		this.queue = [];
		this.waiter = null;
		this.mtls = null;
	}

	feed(chunk)
	{
		for( const f of this.framer.push(chunk) )
		{
			if( this.waiter && this.waiter.pred(f) )
			{
				const w = this.waiter;
				this.waiter = null;
				clearTimeout(w.timer);
				w.resolve(f);
			} else
			{
				this.queue.push(f);
			}
		}
	}

	// Resolve with the first frame matching pred (queued or future).
	next(pred, timeoutMs, what)
	{
		const i = this.queue.findIndex(pred);
		if( i >= 0 ) return Promise.resolve(this.queue.splice(i, 1)[0]);
		if( this.waiter ) return Promise.reject(new Error('internal: overlapping wait'));
		return new Promise((resolve, reject) =>
		{
			const timer = setTimeout(() => { this.waiter = null; reject(new Error(`Timed out waiting for ${what}`)); }, timeoutMs);
			this.waiter = { pred, resolve, timer };
		});
	}

	// Dongle sends B0 (srvPub65 || sid4 BE) a few times after notifications are enabled.
	async waitHello(timeoutMs = 8000)
	{
		const f = await this.next((f) => f.op === 0xB0, timeoutMs, 'HELLO (B0) from dongle');
		if( f.payload.length !== 69 || f.payload[0] !== 0x04 ) throw new Error('Bad HELLO from dongle');
		return f.payload;
	}

	////////////////////////////////////////////////////////////////
	// Provisioning: obtain the AppKey using the dongle's setup password.
	// Returns the raw 32-byte AppKey (caller imports + stores it).
	////////////////////////////////////////////////////////////////
	async provision(password)
	{
		await this.write(frame(0xA0));
		const a2 = await this.next((f) => f.op === 0xA2 || f.op === 0xFF, 6000, 'challenge (A2)');
		if( a2.op === 0xFF ) throw provisionError(a2.payload);
		if( a2.payload.length !== 36 ) throw new Error('Bad challenge from dongle');

		const p = a2.payload;
		const salt = p.slice(0, 16);
		const iters = (p[16] | (p[17] << 8) | (p[18] << 16) | (p[19] << 24)) >>> 0;
		const chal = p.slice(20, 36);

		const verif = await pbkdf2(password, salt, Math.max(1, iters));
		const proof = await hmac(verif, concat(ascii('APPKEY'), chal));

		await this.write(frame(0xA3, proof));
		const a1 = await this.next((f) => f.op === 0xA1 || f.op === 0xFF, 6000, 'AppKey (A1)');
		if( a1.op === 0xFF ) throw provisionError(a1.payload);
		if( a1.payload.length !== 48 ) throw new Error('Bad AppKey reply from dongle');

		// A1 = AES-CTR(wrapKey, iv, appKey) || HMAC(wrapKey, "AKMAC"||chal||cipher)[0..15]
		const cipher = a1.payload.slice(0, 32);
		const wrapKey = await hmac(verif, concat(ascii('AKWRAP'), chal));
		const macExp = await hmac16(wrapKey, concat(ascii('AKMAC'), chal, cipher));
		if( !equal(macExp, a1.payload.slice(32, 48)) ) throw new Error('AppKey reply failed integrity check');
		const iv = (await hmac(verif, concat(ascii('AKIV'), chal))).slice(0, 16);
		const appKey = await aesCtr(wrapKey, iv, cipher);

		verif.fill(0); wrapKey.fill(0);
		return appKey;
	}

	////////////////////////////////////////////////////////////////
	// MTLS handshake from a received B0. appKey: HMAC CryptoKey.
	////////////////////////////////////////////////////////////////
	async handshake(appKey, b0)
	{
		const srvPub = b0.slice(0, 65);
		const sid4 = b0.slice(65, 69);

		const kp = await subtle.generateKey({ name: 'ECDH', namedCurve: 'P-256' }, false, ['deriveBits']);
		const cliPub = new Uint8Array(await subtle.exportKey('raw', kp.publicKey)); // 65 bytes, 0x04||X||Y

		const transcript = concat(sid4, srvPub, cliPub);
		const mac = await hmac16(appKey, concat(ascii('KEYX'), transcript));

		this.queue = this.queue.filter((f) => f.op !== 0xB0); // drop B0 retransmits
		await this.write(frame(0xB1, concat(cliPub, mac)));
		const b2 = await this.next((f) => f.op === 0xB2 || f.op === 0xFF, 6000, 'handshake reply (B2)');
		if( b2.op === 0xFF )
		{
			const e = errText(b2.payload);
			const err = new Error(e === 'BADMAC' ? 'The dongle rejected the stored key' : `Handshake failed: ${e}`);
			err.badKey = (e === 'BADMAC');
			throw err;
		}

		// Z = ECDH x-coordinate (32 bytes)
		const srvKey = await subtle.importKey('raw', srvPub, { name: 'ECDH', namedCurve: 'P-256' }, false, []);
		const z = new Uint8Array(await subtle.deriveBits({ name: 'ECDH', public: srvKey }, kp.privateKey, 256));

		// sessKey = HKDF-SHA256(salt=AppKey, ikm=Z, info="MT1"||sid||srvPub||cliPub), one block
		const prk = await hmac(appKey, z);
		const sess = await hmac(prk, concat(ascii('MT1'), transcript, new Uint8Array([1])));
		const kEnc = await hmac(sess, ascii('ENC'));
		const kMac = await hmac(sess, ascii('MAC'));
		const kIv  = await hmac(sess, ascii('IVK'));

		const sfin = await hmac16(kMac, concat(ascii('SFIN'), transcript));
		if( !equal(sfin, b2.payload) )
		{
			const err = new Error('Dongle failed to prove the session key');
			err.badKey = true;
			throw err;
		}

		this.mtls = {
			sid4,
			kEnc: await subtle.importKey('raw', kEnc, 'AES-CTR', false, ['encrypt']),
			kMac: await importHmacKey(kMac),
			kIv:  await importHmacKey(kIv),
			seqOut: 0,
			seqIn: 0,
		};
		for( const b of [z, prk, sess, kEnc, kMac, kIv] ) b.fill(0);
		this.queue = [];
	}

	async ivFor(dir, seq)
	{
		const m = this.mtls;
		return (await hmac(m.kIv, concat(ascii('IV1'), m.sid4, new Uint8Array([dir]), be16(seq)))).slice(0, 16);
	}

	async wrapB3(inner)
	{
		const m = this.mtls;
		if( !m ) throw new Error('Not connected securely');
		if( m.seqOut >= 0xFFFF ) throw new Error('Session exhausted - reconnect');
		const seq = m.seqOut++;
		const C = 0x43;
		const ct = await aesCtr(m.kEnc, await this.ivFor(C, seq), inner);
		const mac = await hmac16(m.kMac, concat(ascii('ENCM'), m.sid4, new Uint8Array([C]), be16(seq), ct));
		return frame(0xB3, concat(be16(seq), be16(ct.length), ct, mac));
	}

	// Returns the decrypted inner frame {op, payload}, or null if invalid.
	async unwrapB3(p)
	{
		const m = this.mtls;
		if( !m || p.length < 20 ) return null;
		const seq = rdBe16(p, 0), clen = rdBe16(p, 2);
		if( p.length !== 4 + clen + 16 ) return null;
		const ct = p.slice(4, 4 + clen);
		const S = 0x53;
		const mac = await hmac16(m.kMac, concat(ascii('ENCM'), m.sid4, new Uint8Array([S]), be16(seq), ct));
		if( !equal(mac, p.slice(4 + clen)) ) return null;
		if( seq < m.seqIn ) return null; // replay
		m.seqIn = seq + 1;
		const inner = await aesCtr(m.kEnc, await this.ivFor(S, seq), ct);
		if( inner.length < 3 ) return null;
		const len = inner[1] | (inner[2] << 8);
		if( inner.length < 3 + len ) return null;
		return { op: inner[0], payload: inner.slice(3, 3 + len) };
	}

	// Send an encrypted app op and wait for the encrypted reply with op `expect`.
	async request(op, payload, expect, timeoutMs)
	{
		await this.write(await this.wrapB3(frame(op, payload)));
		const deadline = Date.now() + timeoutMs;
		for(;;)
		{
			const f = await this.next((f) => f.op === 0xB3 || f.op === 0xFF, Math.max(1, deadline - Date.now()), 'dongle reply');
			if( f.op === 0xFF ) throw new Error(`Dongle error: ${errText(f.payload)}`);
			const inner = await this.unwrapB3(f.payload);
			if( !inner ) continue;
			if( inner.op === 0xFF ) throw new Error(`Dongle error: ${errText(inner.payload)}`);
			if( inner.op === expect ) return inner.payload;
		}
	}

	async getInfo()
	{
		return new TextDecoder().decode(await this.request(0xC1, new Uint8Array(0), 0xC2, 4000));
	}

	// Type text on the host computer. Resolves once the dongle reports it typed it.
	async sendText(text)
	{
		const bytes = utf8.encode(text);
		if( bytes.length === 0 ) throw new Error('Nothing to send');
		if( bytes.length > MAX_TEXT_BYTES ) throw new Error(`Text too long (max ${MAX_TEXT_BYTES} bytes)`);
		// typing is slow on some layouts (alt-codes), so allow time per character
		const r = await this.request(0xD0, bytes, 0xD1, 6000 + bytes.length * 60);
		if( r.length < 1 || r[0] !== 0 ) throw new Error('Dongle reported a typing error');
	}
}

function provisionError(payload)
{
	const e = errText(payload);
	if( e.includes('LOCKED_SINGLE_NEED_RESET') )
		return new Error('The dongle is already linked to another app and allows only one. Factory-reset the dongle (hold its button 3 s) and run setup again, ticking "allow multiple apps" if you want to keep more than one.');
	if( e === 'bad proof' ) return new Error('Wrong setup password');
	if( e === 'GET_APPKEY blocked' ) return new Error('Too many wrong passwords - unplug and replug the dongle');
	return new Error(`Linking failed: ${e}`);
}
