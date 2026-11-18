// Tests bkproto.js against a simulated dongle.
// The simulator re-implements the firmware side (blue_keyboard/mtls.cpp and
// blue_keyboard/commands.h) with node:crypto, independent of WebCrypto.
// Run: node --test apps/web/test/protocol.test.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import nodeCrypto from 'node:crypto';
import { BkClient, Framer, frame, importHmacKey } from '../bkproto.js';

const SETUP_PW = 'correct horse ü€';
const hmac = (k, ...m) => nodeCrypto.createHmac('sha256', k).update(Buffer.concat(m.map((x) => Buffer.from(x)))).digest();
const ctr = (k, iv, d) => { const c = nodeCrypto.createCipheriv('aes-256-ctr', k, iv); return Buffer.concat([c.update(d), c.final()]); };
const be16 = (v) => Buffer.from([v >> 8, v & 0xFF]);

class SimDongle
{
	constructor()
	{
		this.salt = nodeCrypto.randomBytes(16);
		this.iters = 10000;
		this.verif = nodeCrypto.pbkdf2Sync(Buffer.from(SETUP_PW, 'utf8'), this.salt, this.iters, 32, 'sha256');
		this.appKey = nodeCrypto.randomBytes(32);
		this.appKeySet = false;
		this.typed = [];
		this.sent = [];
	}

	// Called when the client enables notifications: send B0 (twice, like the retry ticker).
	connect(client)
	{
		this.client = client;
		this.active = false;
		this.seqIn = this.seqOut = 0;
		this.ecdh = nodeCrypto.createECDH('prime256v1');
		this.srvPub = this.ecdh.generateKeys(null, 'uncompressed');
		this.sid = nodeCrypto.randomBytes(4);
		const b0 = Buffer.concat([this.srvPub, this.sid]);
		this.notify(frame(0xB0, b0));
		this.notify(frame(0xB0, b0));
	}

	// Deliver to the client in 20-byte chunks (worst-case BLE MTU) to exercise the framer.
	notify(bytes)
	{
		this.sent.push(Buffer.from(bytes));
		for( let i = 0; i < bytes.length; i += 20 )
		{
			const chunk = new Uint8Array(bytes.slice(i, i + 20));
			setImmediate(() => this.client.feed(chunk));
		}
	}

	sendFrame(op, payload = Buffer.alloc(0))
	{
		const f = Buffer.from(frame(op, new Uint8Array(payload)));
		if( !this.active ) return this.notify(f);
		const seq = this.seqOut++;
		const iv = hmac(this.kIv, 'IV1', this.sid, 'S', be16(seq)).subarray(0, 16);
		const ct = ctr(this.kEnc, iv, f);
		const mac = hmac(this.kMac, 'ENCM', this.sid, 'S', be16(seq), ct).subarray(0, 16);
		this.notify(frame(0xB3, new Uint8Array(Buffer.concat([be16(seq), be16(ct.length), ct, mac]))));
	}

	err(msg) { this.sendFrame(0xFF, Buffer.from(msg)); }

	// One GATT write = one complete frame (handleWrite in the .ino requires this).
	receive(bytes)
	{
		const b = Buffer.from(bytes);
		const op = b[0], len = b[1] | (b[2] << 8);
		assert.equal(b.length, 3 + len, 'client must write whole frames');
		this.dispatch(op, b.subarray(3, 3 + len));
	}

	dispatch(op, p)
	{
		if( op === 0xB1 ) return this.onB1(p);
		if( op === 0xB3 ) return this.onB3(p);
		if( !this.active )
		{
			if( op === 0xA0 )
			{
				if( this.appKeySet ) return this.err('LOCKED_SINGLE_NEED_RESET');
				this.chal = nodeCrypto.randomBytes(16);
				const it = Buffer.alloc(4); it.writeUInt32LE(this.iters);
				return this.sendFrame(0xA2, Buffer.concat([this.salt, it, this.chal]));
			}
			if( op === 0xA3 )
			{
				const exp = hmac(this.verif, 'APPKEY', this.chal);
				if( p.length !== 32 || !exp.equals(p) ) return this.err('bad proof');
				const wrapKey = hmac(this.verif, 'AKWRAP', this.chal);
				const iv = hmac(this.verif, 'AKIV', this.chal).subarray(0, 16);
				const c = ctr(wrapKey, iv, this.appKey);
				const mac = hmac(wrapKey, 'AKMAC', this.chal, c).subarray(0, 16);
				this.appKeySet = true;
				return this.sendFrame(0xA1, Buffer.concat([c, mac]));
			}
			return this.err('need MTLS');
		}
		if( op === 0xC1 ) return this.sendFrame(0xC2, Buffer.from('LAYOUT=US_WINLIN; PROTO=1.6; FW=2.1.0'));
		if( op === 0xD0 )
		{
			this.typed.push(p.toString('utf8'));
			return this.sendFrame(0xD1, Buffer.concat([Buffer.from([0]), nodeCrypto.createHash('md5').update(p).digest()]));
		}
		return this.err('bad op');
	}

	onB1(p)
	{
		const cli = p.subarray(0, 65), macIn = p.subarray(65);
		const exp = hmac(this.appKey, 'KEYX', this.sid, this.srvPub, cli).subarray(0, 16);
		if( !exp.equals(macIn) ) return this.err('BADMAC');
		const z = this.ecdh.computeSecret(cli);
		const info = Buffer.concat([Buffer.from('MT1'), this.sid, this.srvPub, cli]);
		const prk = hmac(this.appKey, z);
		const sess = hmac(prk, info, Buffer.from([1]));
		// cross-check the manual HKDF against node's HKDF
		assert.ok(sess.equals(Buffer.from(nodeCrypto.hkdfSync('sha256', z, this.appKey, info, 32))));
		this.kEnc = hmac(sess, 'ENC'); this.kMac = hmac(sess, 'MAC'); this.kIv = hmac(sess, 'IVK');
		this.sendFrame(0xB2, hmac(this.kMac, 'SFIN', this.sid, this.srvPub, cli).subarray(0, 16));
		this.active = true;
	}

	onB3(p)
	{
		if( !this.active ) return this.err('NOSESSION');
		const seq = p.readUInt16BE(0), clen = p.readUInt16BE(2);
		const ct = p.subarray(4, 4 + clen), macIn = p.subarray(4 + clen);
		const mac = hmac(this.kMac, 'ENCM', this.sid, 'C', be16(seq), ct).subarray(0, 16);
		if( !mac.equals(macIn) ) return this.err('BADMAC');
		if( seq !== this.seqIn ) return this.err('REPLAY');
		this.seqIn++;
		const inner = ctr(this.kEnc, hmac(this.kIv, 'IV1', this.sid, 'C', be16(seq)).subarray(0, 16), ct);
		const len = inner[1] | (inner[2] << 8);
		this.dispatch(inner[0], inner.subarray(3, 3 + len));
	}
}

function wire(dongle)
{
	const writes = [];
	const client = new BkClient(async (bytes) => { writes.push(Buffer.from(bytes)); setImmediate(() => dongle.receive(bytes)); });
	dongle.connect(client);
	return { client, writes };
}

test('link, handshake, send text and read info', async () =>
{
	const d = new SimDongle();

	// first connection: provision
	let { client } = wire(d);
	await client.waitHello();
	const raw = await client.provision(SETUP_PW);
	assert.ok(Buffer.from(raw).equals(d.appKey), 'unwrapped AppKey matches the dongle');
	const key = await importHmacKey(raw);

	// reconnect: handshake + app ops
	({ client } = wire(d));
	await client.handshake(key, await client.waitHello());
	const text = 'P@ss wörd€ 🔑\tline2\n';
	await client.sendText(text);
	await client.sendText('second');
	assert.deepEqual(d.typed, [text, 'second']);
	assert.equal(await client.getInfo(), 'LAYOUT=US_WINLIN; PROTO=1.6; FW=2.1.0');
});

test('plaintext never appears on the wire', async () =>
{
	const d = new SimDongle();
	let { client } = wire(d);
	await client.waitHello();
	const key = await importHmacKey(await client.provision(SETUP_PW));
	let writes;
	({ client, writes } = wire(d));
	await client.handshake(key, await client.waitHello());
	const secret = 'SuperSecretPassword123';
	await client.sendText(secret);
	assert.equal(d.typed[0], secret);
	for( const w of writes ) assert.ok(!w.includes(Buffer.from(secret)), 'password leaked in a write');
	for( const w of writes ) assert.ok(!w.includes(Buffer.from('SuperSecret')), 'password prefix leaked in a write');
	for( const w of writes ) assert.ok(!w.includes(Buffer.from(SETUP_PW, 'utf8')), 'setup password leaked');
	for( const s of d.sent ) assert.ok(!s.includes(d.appKey), 'AppKey sent in clear');
});

test('wrong setup password is rejected', async () =>
{
	const d = new SimDongle();
	const { client } = wire(d);
	await client.waitHello();
	await assert.rejects(client.provision('wrong'), /Wrong setup password/);
});

test('second link attempt on a single-app dongle explains the lock', async () =>
{
	const d = new SimDongle();
	const { client } = wire(d);
	await client.waitHello();
	await client.provision(SETUP_PW);
	await assert.rejects(client.provision(SETUP_PW), /already linked/);
});

test('wrong AppKey fails the handshake with badKey', async () =>
{
	const d = new SimDongle();
	const { client } = wire(d);
	const wrong = await importHmacKey(nodeCrypto.randomBytes(32));
	await assert.rejects(client.handshake(wrong, await client.waitHello()), (e) => e.badKey === true);
});

test('client drops replayed or tampered dongle records', async () =>
{
	const d = new SimDongle();
	let { client } = wire(d);
	await client.waitHello();
	const key = await importHmacKey(await client.provision(SETUP_PW));
	({ client } = wire(d));
	await client.handshake(key, await client.waitHello());
	await client.sendText('a');
	const lastB3 = d.sent.at(-1).subarray(3);
	assert.equal(await client.unwrapB3(new Uint8Array(lastB3)), null, 'replay accepted');
	const tampered = new Uint8Array(lastB3); tampered[5] ^= 1;
	assert.equal(await client.unwrapB3(tampered), null, 'tampered record accepted');
});

test('framer reassembles split frames and skips garbage', () =>
{
	const f = new Framer();
	const a = frame(0xB0, new Uint8Array(69).fill(7));
	const out = [...f.push(new Uint8Array([0x41, 0x42, 0xFF, 0xFF])), ...f.push(a.slice(0, 10)), ...f.push(a.slice(10))];
	assert.equal(out.length, 1);
	assert.equal(out[0].op, 0xB0);
	assert.equal(out[0].payload.length, 69);
});
