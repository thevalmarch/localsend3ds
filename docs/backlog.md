# Deferred product backlog

This file records work intentionally left beyond the current v1.0.0 scope. The
following foundations are complete and have been exercised on a real New
Nintendo 2DS XL with official LocalSend peers on macOS and Linux as recorded in
the compatibility matrix:

- bidirectional discovery and one-file transfer over the supported HTTP receive
  and HTTP/HTTPS outgoing paths;
- persistent mutual-TLS client identity and fingerprint-pinned outgoing HTTPS
  interoperability with official LocalSend on macOS and Linux;
- a dual-screen Citro2D interface with touch and physical controls;
- graphical receive approval, transfer progress, results, SD browsing, and
  recipient selection;
- configurable device alias plus persistent Quick Save and Auto Finish
  settings;
- `.3dsx` and native CIA packaging from the same application source;
- direct native CIA launch from HOME Menu without a forwarder or external
  `.3dsx` dependency;
- original LocalSend3DS icon, HOME Menu banner, and menu chime.

Still deferred:

- incoming HTTPS-server support, PIN support, and trusted-device policy;
- post-v1 migration from devkitPro's current `3ds-mbedtls` 2.28.8 package to a
  newer supported package when one is available without sacrificing build
  reproducibility;
- multiple-file, folder, text, and clipboard transfers;
- transfer history and broader settings;
- interoperability testing with official LocalSend clients on Windows,
  Android, and iOS;
- testing on Nintendo 3DS-family models other than New Nintendo 2DS XL;
- broader cancellation, network-interruption, low-storage, lid/sleep, and large
  file test coverage;
- additional UI refinements supported by real-hardware observations.

Official LocalSend logo artwork is not bundled. The project uses its original
LocalSend3DS mark unless upstream provides clear permission for a different
branding arrangement.
