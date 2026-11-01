# A Marstek CT on its own Marsrelay

A Marstek CT (CT002, CT003) can join a Marsrelay access point to stay off the cloud too, even when it is too far from the battery to share one. Each device then gets its own Marsrelay, both connected to the same home WiFi:

- **The battery's Marsrelay** keeps the usual `udp_proxy` on port `12345`: it passes the battery's requests to the home network.
- **The CT's Marsrelay** passes them on to the CT and sends the answers back. Give its `udp_proxy` the same port and tell it the meter is on its access point:

  ```yaml
  udp_proxy:
    - id: meter_proxy
      port: 12345
      meter_network: access_point
  ```

`meter_network` defaults to `home`, the usual case of a meter on your home network answering a battery on the access point. A battery on your home network works with the CT's Marsrelay directly, without a Marsrelay of its own.

The two Marsrelays find each other by broadcast, so they need to be on the same home network segment: not separated by VLANs, guest WiFi or client isolation.
