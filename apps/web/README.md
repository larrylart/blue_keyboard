# Blue Keyboard – Web App (Web Bluetooth)

A single static web page that sends text and passwords to the Blue Keyboard dongle over Bluetooth.
It speaks the same encrypted protocol as the Android, iOS and Linux clients, so the **firmware does not
need any changes**.

Works in:
- **iPhone / iPad**: Safari with the free [beacio](https://apps.apple.com/app/id6761301368) Safari extension (iOS 26.2 or later).
- **Desktop and Android**: Chrome or Edge (built-in Web Bluetooth).

## Security

- Before anything goes over Bluetooth, the text is encrypted **in the page** with the dongle's MTLS protocol
  (P-256 key exchange for each session, AES-CTR + HMAC-SHA256, replay protection). See
  [SECURITY_OVERVIEW.md](../../SECURITY_OVERVIEW.md). On top of that, the Bluetooth link itself is
  encrypted by passkey pairing.
- So the browser, the beacio extension and the Bluetooth stack only ever handle ciphertext.
  An extension that has access to the page could still read the input field *before* encryption, so give beacio
  access to this site only rather than to all websites, if that works on your iOS version.
- The AppKey is stored in the browser's IndexedDB as a **non-extractable** WebCrypto key. Page scripts can
  use it but can never read its bytes back. Only the dongle and this page on this origin have it.
- Only WebCrypto is used (`crypto.subtle`): no third-party libraries, no network requests besides loading
  the page itself.

## Hosting

The page is three static files: `index.html`, `app.js` and `bkproto.js`. Host them on any web server:

1. **HTTPS with a valid certificate** (for example Let's Encrypt). Web Bluetooth and WebCrypto only work in a
   secure context, and beacio only runs on secure pages.
2. **Its own hostname** (for example `bk.example.com`), not a subfolder of a site that runs other
   apps. The stored key belongs to the origin, so anything else served from the same origin could use it.
3. Serve `.js` files as `text/javascript` (most servers do this by default; module scripts refuse anything else).
4. Treat the server as part of the trust chain: whoever can change these files can change what the page does.

Recommended response headers (nginx example):

```nginx
server {
    listen 443 ssl;
    server_name bk.example.com;
    root /var/www/blue-keyboard;          # contains index.html, app.js, bkproto.js

    add_header Strict-Transport-Security "max-age=31536000" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header Referrer-Policy "no-referrer" always;
    add_header Content-Security-Policy "frame-ancestors 'none'" always;
    add_header Cache-Control "no-cache" always;   # pick up updates immediately
}
```

## First use (iPhone)

1. Install **beacio** from the App Store, open it once, then in Safari: `aA` → *Manage Extensions* → enable beacio,
   and give it access to your page's site.
2. Open your page in Safari. Not from a Home Screen icon: beacio only works in a Safari tab.
3. Enter the dongle's **setup password** (the one you chose in the dongle's Wi-Fi setup portal), tap **Connect**
   and pick the dongle.
4. iOS asks to pair: enter the PIN shown on the dongle's screen (or the PIN you chose during setup for
   dongles without a screen).
5. The page links itself (fetches the AppKey), reconnects and shows **Connected**. Next time you only tap Connect.

To send: tap the text field. Safari offers **AutoFill / Passwords**, so you can pick a password from iCloud
Keychain or your password manager without the clipboard. Tick *Press Enter after* if wanted, then tap **Send**.
If iOS dropped the Bluetooth link while Safari was in the background, Send reconnects by itself.

## Things to know

- **One app per dongle by default.** If the dongle was set up without "allow multiple apps" and another app (or an
  earlier copy of this page) already has the key, linking fails with a message saying so. Factory-reset the dongle
  (hold its button for 3 s) and run its setup again.
- **Safari may delete stored data** for sites you haven't opened in 7 days of Safari use. If that happens the page
  asks for the setup password again, and on a single-app dongle that means a factory reset. Ticking "allow multiple
  apps" during dongle setup lets you re-link with only the password.
- *Forget this dongle's key* removes the stored key from this browser.
- Max text per send is 480 bytes (one Bluetooth write).
- The dongle's MD5 "what was typed" check is not used. WebCrypto has no MD5, and the encrypted channel already
  guarantees the text arrived intact. The page checks the dongle's success status.

## Tests

`test/protocol.test.mjs` runs the page's protocol code against a simulated dongle. The simulator is an
independent `node:crypto` implementation of `mtls.cpp` / `commands.h`.

```bash
node --test apps/web/test/protocol.test.mjs
```
