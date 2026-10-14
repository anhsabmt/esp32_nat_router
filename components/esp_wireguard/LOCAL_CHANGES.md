# Vendored WireGuard

Source: https://github.com/martin-ger/esp_wireguard_idf_5.5.x

Pinned upstream revision: `02a24fad29365db09e0088df2f8c4414632fe35a`.
The original BSD license and copyright notices are retained.

Local changes for profile switching:

- Publish the netif in the context immediately after creation, so a peer-init failure can be cleaned up.
- Make disconnect safe when no netif exists; wipe decoded PSK and peer state after teardown.
- Accept already-resolved endpoint addresses without invoking a blocking DNS API from the TCP/IP thread.
- Remove PSK debug logging and wipe the decoded private key and freed device state.
- Seed the shared random generator once, avoiding leaked entropy allocations on repeated initialization.

The router invokes lifecycle operations through `tcpip_callback_wait`. Hostname resolution runs outside the TCP/IP thread, guarded by a controller generation check.
