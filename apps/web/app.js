////////////////////////////////////////////////////////////////////
// app.js - UI + Web Bluetooth glue for the Blue Keyboard web client
////////////////////////////////////////////////////////////////////
import { BkClient, importHmacKey, SERVICE_UUID, CHAR_WRITE_UUID, CHAR_NOTIFY_UUID, MAX_TEXT_BYTES } from './bkproto.js';

const $ = (id) => document.getElementById(id);
const ui = {
	status: $('status'), info: $('info'),
	linkBox: $('linkBox'), setupPw: $('setupPw'),
	connect: $('connect'), disconnect: $('disconnect'),
	text: $('text'), reveal: $('reveal'), enter: $('enter'), send: $('send'),
	result: $('result'), forget: $('forget'), unsupported: $('unsupported'),
};

let device = null;   // BluetoothDevice (kept so we can reconnect without the picker)
let client = null;   // BkClient with an active MTLS session, or null
let busy = false;

////////////////////////////////////////////////////////////////////
// AppKey storage: IndexedDB, as a non-extractable CryptoKey
////////////////////////////////////////////////////////////////////
function db()
{
	return new Promise((resolve, reject) =>
	{
		const r = indexedDB.open('blue-keyboard', 1);
		r.onupgradeneeded = () => r.result.createObjectStore('kv');
		r.onsuccess = () => resolve(r.result);
		r.onerror = () => reject(r.error);
	});
}

async function kv(mode, fn)
{
	const d = await db();
	return new Promise((resolve, reject) =>
	{
		const tx = d.transaction('kv', mode);
		const req = fn(tx.objectStore('kv'));
		tx.oncomplete = () => { d.close(); resolve(req.result); };
		tx.onerror = () => { d.close(); reject(tx.error); };
	});
}

const loadKey  = () => kv('readonly',  (s) => s.get('appkey'));
const saveKey  = (key) => kv('readwrite', (s) => s.put(key, 'appkey'));
const clearKey = () => kv('readwrite', (s) => s.delete('appkey'));

////////////////////////////////////////////////////////////////////
// UI helpers
////////////////////////////////////////////////////////////////////
function setStatus(text, state)
{
	ui.status.textContent = text;
	ui.status.dataset.state = state; // idle | busy | ok | error
}

function setResult(text, ok)
{
	ui.result.textContent = text;
	ui.result.dataset.state = ok ? 'ok' : 'error';
}

async function refresh()
{
	const connected = !!client;
	const hasKey = !!(await loadKey().catch(() => null));
	ui.linkBox.hidden = hasKey;
	ui.connect.hidden = connected;
	ui.disconnect.hidden = !connected;
	ui.connect.disabled = busy;
	ui.send.disabled = busy || (!connected && !device);
	ui.forget.hidden = !hasKey;
	if( !connected ) ui.info.textContent = '';
}

////////////////////////////////////////////////////////////////////
// Connection
////////////////////////////////////////////////////////////////////
async function openGatt()
{
	const server = await device.gatt.connect();
	const svc = await server.getPrimaryService(SERVICE_UUID);
	const tx = await svc.getCharacteristic(CHAR_NOTIFY_UUID);
	const rx = await svc.getCharacteristic(CHAR_WRITE_UUID);

	// one frame = one write; prefer with-response so long frames use a GATT long write
	const c = new BkClient((bytes) => rx.writeValueWithResponse ? rx.writeValueWithResponse(bytes) : rx.writeValue(bytes));
	tx.addEventListener('characteristicvaluechanged', (e) =>
	{
		const v = e.target.value;
		c.feed(new Uint8Array(v.buffer, v.byteOffset, v.byteLength));
	});
	// Enabling notifications triggers iOS pairing (PIN) on first use; the
	// dongle then starts sending its HELLO (B0).
	await tx.startNotifications();
	return c;
}

// Connect + (if needed) link + MTLS handshake. Leaves `client` set on success.
async function establish()
{
	let key = await loadKey();
	let c = await openGatt();
	const b0 = await c.waitHello(key ? 8000 : 10000);

	if( !key )
	{
		const pw = ui.setupPw.value;
		if( !pw ) throw new Error('Enter the dongle setup password to link this page');
		setStatus('Linking…', 'busy');
		const raw = await c.provision(pw);
		key = await importHmacKey(raw);
		raw.fill(0);
		await saveKey(key);
		ui.setupPw.value = '';
		// The dongle only sends HELLO right after connecting: reconnect for the handshake.
		device.gatt.disconnect();
		setStatus('Linked. Reconnecting…', 'busy');
		c = await openGatt();
		await doHandshake(c, key, await c.waitHello(8000));
	} else
	{
		await doHandshake(c, key, b0);
	}
	client = c;
}

async function doHandshake(c, key, b0)
{
	setStatus('Securing…', 'busy');
	try
	{
		await c.handshake(key, b0);
	} catch( e )
	{
		if( e.badKey )
		{
			await clearKey();
			throw new Error('The dongle no longer accepts the stored key (was it reset?). Enter its setup password and connect again.');
		}
		throw e;
	}
}

function onDisconnected()
{
	client = null;
	// keep error / progress messages; only replace a "connected" status
	if( ui.status.dataset.state === 'ok' ) setStatus('Disconnected', 'idle');
	refresh();
}

async function run(label, fn)
{
	if( busy ) return;
	busy = true;
	refresh();
	try
	{
		await fn();
	} catch( e )
	{
		client = null;
		if( device?.gatt?.connected ) device.gatt.disconnect();
		setStatus(e.message || String(e), 'error');
		console.error(label, e);
	} finally
	{
		busy = false;
		refresh();
	}
}

async function connectFlow(pick)
{
	if( pick || !device )
	{
		device = await navigator.bluetooth.requestDevice({ filters: [{ services: [SERVICE_UUID] }] });
		device.addEventListener('gattserverdisconnected', onDisconnected);
	}
	setStatus(`Connecting to ${device.name || 'dongle'}…`, 'busy');
	await establish();
	setStatus(`Connected to ${device.name || 'dongle'}`, 'ok');
	try
	{
		const info = await client.getInfo(); // "LAYOUT=..; PROTO=..; FW=.."
		ui.info.textContent = info.replace(/;\s*/g, ' · ');
	} catch { /* informational only */ }
}

////////////////////////////////////////////////////////////////////
// Events
////////////////////////////////////////////////////////////////////
ui.connect.addEventListener('click', () =>
{
	if( !ui.linkBox.hidden && !ui.setupPw.value ) { setStatus('Enter the dongle setup password first', 'error'); ui.setupPw.focus(); return; }
	run('connect', () => connectFlow(true));
});

ui.disconnect.addEventListener('click', () =>
{
	client = null;
	if( device?.gatt?.connected ) device.gatt.disconnect();
	setStatus('Disconnected', 'idle');
	refresh();
});

ui.reveal.addEventListener('change', () => { ui.text.type = ui.reveal.checked ? 'text' : 'password'; });

ui.text.addEventListener('keydown', (e) => { if( e.key === 'Enter' ) { e.preventDefault(); ui.send.click(); } });

ui.send.addEventListener('click', () => run('send', async () =>
{
	const chars = [...ui.text.value].length;
	const text = ui.text.value + (ui.enter.checked ? '\n' : '');
	if( !chars ) { setResult('Nothing to send', false); return; }
	if( new TextEncoder().encode(text).length > MAX_TEXT_BYTES ) { setResult(`Too long (max ${MAX_TEXT_BYTES} bytes)`, false); return; }

	// iOS drops the BLE link when Safari goes to the background: reconnect silently.
	if( !client ) await connectFlow(false);

	setResult('Typing…', true);
	try
	{
		await client.sendText(text);
	} catch( e )
	{
		setResult(e.message, false);
		throw e;
	}
	ui.text.value = '';
	ui.reveal.checked = false;
	ui.text.type = 'password';
	setResult(`Typed ${chars} characters${ui.enter.checked ? ' + Enter' : ''}`, true);
}));

ui.forget.addEventListener('click', async () =>
{
	if( !confirm('Forget the stored key for this dongle? You will need its setup password to link again.') ) return;
	await clearKey();
	ui.disconnect.click();
	setStatus('Key forgotten', 'idle');
	refresh();
});

////////////////////////////////////////////////////////////////////
// Startup checks
////////////////////////////////////////////////////////////////////
if( !window.isSecureContext )
{
	ui.unsupported.hidden = false;
	ui.unsupported.textContent = 'This page must be served over HTTPS.';
	ui.connect.disabled = true;
} else if( !navigator.bluetooth )
{
	ui.unsupported.hidden = false;
	ui.connect.disabled = true;
} else
{
	setStatus('Not connected', 'idle');
	refresh();
}
