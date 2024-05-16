#include <am.h>
#include <klib.h>
#include <klib-macros.h>

extern int image_width, image_height;
extern unsigned int image_data[];

int main() {
    printf("Hello, OS World!\n");
    ioe_init();
    if (io_read(AM_GPU_STATUS).ready) {
        printf("GPU Ready\n");
    }
    int w = io_read(AM_GPU_CONFIG).width;
    int h = io_read(AM_GPU_CONFIG).height;
    printf("video size: %d, %d\n", w, h);
    printf("image size: %d, %d\n", image_width, image_height);
    
    // 0x0090cdfa = 9489914
    printf("first pixel: %d\n", image_data[0]);

    int x, y;
    for (y = 0; y < h; y ++) {
        for (x = 0; x < w; x ++) {
            int map_x = x * image_width / w;
            int map_y = y * image_height / h;
            io_write(AM_GPU_FBDRAW, x, y, &image_data[map_y * image_width + map_x], 1, 1, false);
        }
    }

    while (1) {
        int key = io_read(AM_INPUT_KEYBRD).keycode;
        if (key == AM_KEY_NONE) continue;
        printf("key: %d\n", key);
        if (key == AM_KEY_ESCAPE || key == AM_KEY_D) break;
    }
}
