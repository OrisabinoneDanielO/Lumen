# Decisions

Choices made while building, with the reasons. The fixed decisions are in `CLAUDE.md` and the design doc.

## Batch 9: media

### Videos are decoded in the render process

The UI process only sends file paths (and preload requests) over the render link. `lumen-render` opens, decodes and draws each video itself, with Qt Multimedia's FFmpeg backend.

- **Why:** the decoded frames stay on the GPU next to the compositor (VA-API, D3D11 or VideoToolbox textures), with no copy of 1080p frames between processes. A UI crash or freeze cannot stop a video, which is the same reason the renderer is separate in the first place.
- **Cost:** the UI does not see the frames. Preview and Live show a thumbnail of the video (made on import), not the moving picture.
- The UI process decodes one frame per file when importing, only to make the thumbnail and read the length.

### Preload in Preview

When the slide in Preview (the one Space sends next) is a video, the UI asks the renderer to `preload` it. The renderer opens it in a hidden player and decodes the first frame, and keeps the last two. Measured with `tst_video_start` (renderer receiving the scene to first frame, offscreen): 1080p VP9 took about 145 ms cold and 25 to 32 ms preloaded. The requirement is under 200 ms from the click; the UI adds under 50 ms (batch 5).

### Reference PC

The "2-hour loop with zero dropped frames" test runs on the developer's laptop: Intel i5-1155G7 with Iris Xe graphics, 16 GB RAM, Fedora 45, Wayland, OpenGL RHI, VA-API (intel-media-driver). The result is in `docs/test-results/`.

### Codecs

On Fedora the free Intel driver decodes VP9 (and AV1/HEVC on newer chips) in hardware but H.264 is decoded in software (OpenH264), because of patents. Windows (D3D11/DXVA) and macOS (VideoToolbox) decode H.264 in hardware. The soak test uses a VP9 loop so the reference run uses the hardware path; `LUMEN_REQUIRE_HW=1` makes it fail if any frame does not.

### How dropped frames are counted

Each frame's presentation time says where it sits in the video. If the next frame shown is more than 1.5 frame lengths later, the frames in between were skipped. A jump back to the start is a loop; frames missing before the end or after the start count as dropped. A gap of more than 100 ms between shown frames counts as a stall.

### Motion backgrounds

**Use as background** plays a video looped and muted on the Background layer. While the operator has chosen a background, themed items no longer replace it with their theme's background; Clear Background (F1) hands control back to the themes. Videos sent as slides go on the Media layer with sound and play once; Clear Slide stops them too.
