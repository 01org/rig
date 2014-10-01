#include <cogl/cogl.h>
#include <cogl-path/cogl-path.h>

#include <string.h>

#include "test-utils.h"

#define BLOCK_SIZE 16

/* Number of pixels at the border of a block quadrant to skip when verifying */
#define TEST_INSET 1

typedef struct _TestState
{
  int dummy;
} TestState;

static void
draw_path_at (cg_path_t *path, cg_pipeline_t *pipeline, int x, int y)
{
  cg_framebuffer_push_matrix (test_fb);
  cg_framebuffer_translate (test_fb, x * BLOCK_SIZE, y * BLOCK_SIZE, 0.0f);

  cg_path_fill (path, test_fb, pipeline);

  cg_framebuffer_pop_matrix (test_fb);
}

static void
check_block (int block_x, int block_y, int block_mask)
{
  uint32_t data[BLOCK_SIZE * BLOCK_SIZE];
  int qx, qy;

  /* Block mask represents which quarters of the block should be
     filled. The bits from 0->3 represent the top left, top right,
     bottom left and bottom right respectively */

  cg_framebuffer_read_pixels (test_fb,
                                block_x * BLOCK_SIZE,
                                block_y * BLOCK_SIZE,
                                BLOCK_SIZE, BLOCK_SIZE,
                                CG_PIXEL_FORMAT_RGBA_8888_PRE,
                                (uint8_t *)data);

  for (qy = 0; qy < 2; qy++)
    for (qx = 0; qx < 2; qx++)
      {
        int bit = qx | (qy << 1);
	const char *intended_pixel = ((block_mask & (1 << bit)) ? "#ffffff" : "#000000");
        int x, y;

        for (x = 0; x < BLOCK_SIZE / 2 - TEST_INSET * 2; x++)
          for (y = 0; y < BLOCK_SIZE / 2 - TEST_INSET * 2; y++)
            {
              const uint32_t *p = data + (qx * BLOCK_SIZE / 2 +
                                        qy * BLOCK_SIZE * BLOCK_SIZE / 2 +
                                        (x + TEST_INSET) +
                                        (y + TEST_INSET) * BLOCK_SIZE);
	      char *screen_pixel = c_strdup_printf ("#%06x", C_UINT32_FROM_BE (*p) >> 8);
	      c_assert_cmpstr (screen_pixel, ==, intended_pixel);
	      c_free (screen_pixel);
            }
      }
}

static void
paint (TestState *state)
{
  cg_path_t *path_a, *path_b, *path_c;
  cg_pipeline_t *white = cg_pipeline_new (test_dev);

  cg_pipeline_set_color4f (white, 1, 1, 1, 1);

  /* Create a path filling just a quarter of a block. It will use two
     rectangles so that we have a sub path in the path */
  path_a = cg_path_new (test_dev);
  cg_path_rectangle (path_a,
                       BLOCK_SIZE * 3 / 4, BLOCK_SIZE / 2,
                       BLOCK_SIZE, BLOCK_SIZE);
  cg_path_rectangle (path_a,
                       BLOCK_SIZE / 2, BLOCK_SIZE / 2,
                       BLOCK_SIZE * 3 / 4, BLOCK_SIZE);
  draw_path_at (path_a, white, 0, 0);

  /* Create another path filling the whole block */
  path_b = cg_path_new (test_dev);
  cg_path_rectangle (path_b, 0, 0, BLOCK_SIZE, BLOCK_SIZE);
  draw_path_at (path_b, white, 1, 0);

  /* Draw the first path again */
  draw_path_at (path_a, white, 2, 0);

  /* Draw a copy of path a */
  path_c = cg_path_copy (path_a);
  draw_path_at (path_c, white, 3, 0);

  /* Add another rectangle to path a. We'll use line_to's instead of
     cg_rectangle so that we don't create another sub-path because
     that is more likely to break the copy */
  cg_path_line_to (path_a, 0, BLOCK_SIZE / 2);
  cg_path_line_to (path_a, 0, 0);
  cg_path_line_to (path_a, BLOCK_SIZE / 2, 0);
  cg_path_line_to (path_a, BLOCK_SIZE / 2, BLOCK_SIZE / 2);
  draw_path_at (path_a, white, 4, 0);

  /* Draw the copy again. It should not have changed */
  draw_path_at (path_c, white, 5, 0);

  /* Add another rectangle to path c. It will be added in two halves,
     one as an extension of the previous path and the other as a new
     sub path */
  cg_path_line_to (path_c, BLOCK_SIZE / 2, 0);
  cg_path_line_to (path_c, BLOCK_SIZE * 3 / 4, 0);
  cg_path_line_to (path_c, BLOCK_SIZE * 3 / 4, BLOCK_SIZE / 2);
  cg_path_line_to (path_c, BLOCK_SIZE / 2, BLOCK_SIZE / 2);
  cg_path_rectangle (path_c,
                       BLOCK_SIZE * 3 / 4, 0, BLOCK_SIZE, BLOCK_SIZE / 2);
  draw_path_at (path_c, white, 6, 0);

  /* Draw the original path again. It should not have changed */
  draw_path_at (path_a, white, 7, 0);

  cg_object_unref (path_a);
  cg_object_unref (path_b);
  cg_object_unref (path_c);

  /* Draw a self-intersecting path. The part that intersects should be
     inverted */
  path_a = cg_path_new (test_dev);
  cg_path_rectangle (path_a, 0, 0, BLOCK_SIZE, BLOCK_SIZE);
  cg_path_line_to (path_a, 0, BLOCK_SIZE / 2);
  cg_path_line_to (path_a, BLOCK_SIZE / 2, BLOCK_SIZE / 2);
  cg_path_line_to (path_a, BLOCK_SIZE / 2, 0);
  cg_path_close (path_a);
  draw_path_at (path_a, white, 8, 0);
  cg_object_unref (path_a);

  /* Draw two sub paths. Where the paths intersect it should be
     inverted */
  path_a = cg_path_new (test_dev);
  cg_path_rectangle (path_a, 0, 0, BLOCK_SIZE, BLOCK_SIZE);
  cg_path_rectangle (path_a,
                       BLOCK_SIZE / 2, BLOCK_SIZE / 2, BLOCK_SIZE, BLOCK_SIZE);
  draw_path_at (path_a, white, 9, 0);
  cg_object_unref (path_a);

  /* Draw a clockwise outer path */
  path_a = cg_path_new (test_dev);
  cg_path_move_to (path_a, 0, 0);
  cg_path_line_to (path_a, BLOCK_SIZE, 0);
  cg_path_line_to (path_a, BLOCK_SIZE, BLOCK_SIZE);
  cg_path_line_to (path_a, 0, BLOCK_SIZE);
  cg_path_close (path_a);
  /* Add a clockwise sub path in the upper left quadrant */
  cg_path_move_to (path_a, 0, 0);
  cg_path_line_to (path_a, BLOCK_SIZE / 2, 0);
  cg_path_line_to (path_a, BLOCK_SIZE / 2, BLOCK_SIZE / 2);
  cg_path_line_to (path_a, 0, BLOCK_SIZE / 2);
  cg_path_close (path_a);
  /* Add a counter-clockwise sub path in the upper right quadrant */
  cg_path_move_to (path_a, BLOCK_SIZE / 2, 0);
  cg_path_line_to (path_a, BLOCK_SIZE / 2, BLOCK_SIZE / 2);
  cg_path_line_to (path_a, BLOCK_SIZE, BLOCK_SIZE / 2);
  cg_path_line_to (path_a, BLOCK_SIZE, 0);
  cg_path_close (path_a);
  /* Retain the path for the next test */
  draw_path_at (path_a, white, 10, 0);

  /* Draw the same path again with the other fill rule */
  cg_path_set_fill_rule (path_a, CG_PATH_FILL_RULE_NON_ZERO);
  draw_path_at (path_a, white, 11, 0);

  cg_object_unref (path_a);
}

static void
validate_result ()
{
  check_block (0, 0, 0x8 /* bottom right */);
  check_block (1, 0, 0xf /* all of them */);
  check_block (2, 0, 0x8 /* bottom right */);
  check_block (3, 0, 0x8 /* bottom right */);
  check_block (4, 0, 0x9 /* top left and bottom right */);
  check_block (5, 0, 0x8 /* bottom right */);
  check_block (6, 0, 0xa /* bottom right and top right */);
  check_block (7, 0, 0x9 /* top_left and bottom right */);
  check_block (8, 0, 0xe /* all but top left */);
  check_block (9, 0, 0x7 /* all but bottom right */);
  check_block (10, 0, 0xc /* bottom two */);
  check_block (11, 0, 0xd /* all but top right */);
}

void
test_path (void)
{
  TestState state;

  cg_framebuffer_orthographic (test_fb,
                                 0, 0,
                                 cg_framebuffer_get_width (test_fb),
                                 cg_framebuffer_get_height (test_fb),
                                 -1,
                                 100);

  paint (&state);
  validate_result ();

  if (cg_test_verbose ())
    c_print ("OK\n");
}

