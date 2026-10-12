# Test-only TLS fixtures

These certificates and the localhost private key are public, disposable test data.
Never use this key or CA for a real server, user data, signing, or distribution.

The integration test runs a TLS server only on loopback. The server certificate
is a self-signed test root, explicitly trusted only by the successful test
requests. This avoids an external CRL service while keeping Schannel's default
certificate, hostname, and revocation settings enabled. The fixture explicitly passes
`test-ca.pem` to the client for successful HTTPS cases, and checks rejection of
untrusted certificates and incorrect hostnames. The server certificate covers
`localhost` only, intentionally excluding `127.0.0.1` for the hostname test.
No certificates are installed into the Windows trust store.

Generated with OpenSSL on 2026-10-12, valid for ten years. `test-ca.pem` and
`localhost-cert.pem` contain the same test certificate. Its private key is
intentionally public and disposable.
