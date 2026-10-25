/*
 * Exult for New 3DS - stereoscopic 3D.
 *
 * While the 3D slider is up, Exult keeps a per-pixel height map next to its
 * 8-bit screen buffer: every world shape stamps its pixels with its height
 * (lift, rising towards the top of tall shapes such as trees, walls and
 * people), and windows/text stamp "screen plane". The SDL backend then asks
 * for a right-eye image, which is the left image re-projected by height.
 * With the slider down none of this runs.
 *
 * GPL v2 (same as Exult).
 */
#ifndef N3DS_STEREO_H
#define N3DS_STEREO_H

class Image_buffer8;

namespace n3ds {
	extern bool depth_active;    // true while the 3D slider is up (in game)

	void depth_begin_world(int x, int y, int w, int h);    // clears rect to ground
	void depth_end_world();
	void depth_object(int lift, int height3d);    // for the next world shape
	void depth_stamp_rle(const Image_buffer8* win, int xoff, int yoff, const unsigned char* rle);
	void depth_stamp_rect(const Image_buffer8* win, int x, int y, int w, int h);
	void depth_copy(const Image_buffer8* win, int sx, int sy, int w, int h, int dx, int dy);

	void stereo_tick();    // once per frame, after present
}    // namespace n3ds

#endif
