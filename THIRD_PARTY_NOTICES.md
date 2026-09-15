# Third-party notices

This file is the release checklist for bundled dependencies. Exact versions and
license texts must be filled in by the packaging workflow before publishing a
binary release.

| Component | Intended use | License/source |
|---|---|---|
| Qt 6 | UI and desktop integration | LGPL/GPL, https://www.qt.io/licensing |
| FFmpeg | Capture, encode and mux backend | LGPL/GPL depending on build, https://ffmpeg.org/legal.html |
| nlohmann/json 3.11.x | JSON settings | MIT, https://github.com/nlohmann/json |

Release builds must not include FFmpeg non-free components. The selected FFmpeg
configuration and its corresponding license obligations are part of the release
artifact metadata.
