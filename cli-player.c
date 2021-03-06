#include "gst/gstcaps.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstformat.h"
#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>

typedef struct
{
  GMainLoop *loop;
  GstElement *pipeline;
  GstElement *asink;
  GstElement *vsink;
} player_t;


static gboolean
cb_bus (GstBus * bus, GstMessage * msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR:{
      gchar *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

void
print_menu ()
{
  g_print ("\nMenu options\n");
  g_print ("==============\n");
  g_print ("%-20s %s", "Pause/Play", "<space>\n");
  g_print ("%-20s %s", "Forward", "f\n");
  g_print ("%-20s %s", "Rewind", "r\n");
  g_print ("%-20s %s", "Quit", "q\n");
  g_print ("\n\n");
}

static gboolean
cb_print_position (GstElement * pipeline)
{
  gint64 pos, len;

  if (gst_element_query_position (pipeline, GST_FORMAT_TIME, &pos)
      && gst_element_query_duration (pipeline, GST_FORMAT_TIME, &len)) {
    g_print (" Time: %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\r",
        GST_TIME_ARGS (pos), GST_TIME_ARGS (len));
  }

  /* call me again */
  return TRUE;
}

static gboolean
cb_io_watch (GIOChannel * ch, GIOCondition cond, gpointer user_data)
{

  GIOStatus status;
  player_t *player = (player_t *) user_data;

  if (cond & G_IO_IN) {
    gchar buf[16];
    gsize bytes_read;
    GError *error = NULL;
    status =
        g_io_channel_read_chars (ch, buf, sizeof (buf), &bytes_read, &error);
    if (status == G_IO_STATUS_ERROR) {
      g_print ("Error reading IO..\n");
      return FALSE;
    } else if (status == G_IO_STATUS_NORMAL) {
      switch (buf[0]) {
        case ' ':
        {
          GstState curr_state, pending_state;
          gst_element_get_state (player->pipeline, &curr_state, &pending_state,
              GST_CLOCK_TIME_NONE);
          //todo: check return val of get_state
          if (curr_state == GST_STATE_PLAYING
              || pending_state == GST_STATE_PLAYING) {
            gst_element_set_state (player->pipeline, GST_STATE_PAUSED);
          } else if (curr_state == GST_STATE_PAUSED
              || pending_state == GST_STATE_PAUSED) {
            gst_element_set_state (player->pipeline, GST_STATE_PLAYING);
          }
        }
          break;
        case 'f':
        {
          gint64 time_nanoseconds = 5000000000;
          gint64 curr_pos = 0;
          gst_element_query_position (player->pipeline, GST_FORMAT_TIME,
              &curr_pos);

          if (!gst_element_seek_simple (player->pipeline, GST_FORMAT_TIME,
                  GST_SEEK_FLAG_FLUSH, curr_pos + time_nanoseconds)) {
            g_print ("\nSeek failed!\n");
          } else {
            g_print ("\n Forward 5 sec successful\n");
          }
        }
          break;
        case 'r':
        {
          gint64 time_nanoseconds = 5000000000;
          gint64 curr_pos = 0;
          gst_element_query_position (player->pipeline, GST_FORMAT_TIME,
              &curr_pos);

          if (!gst_element_seek_simple (player->pipeline, GST_FORMAT_TIME,
                  GST_SEEK_FLAG_FLUSH, curr_pos - time_nanoseconds)) {
            g_print ("\nSeek failed!\n");
          } else {
            g_print ("\nRewind 5 sec successful\n");
          }
        }
          break;
        case 'q':
        case 'Q':
          g_main_loop_quit (player->loop);
          break;
      }
    }
    print_menu ();
  }
  return TRUE;
}

static void
cb_pad_added (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad = NULL;
  GstCaps *caps;
  gchar *caps_str;
  player_t *player = (player_t *) data;

  g_print ("Dynamic pad created\n");

  caps = gst_pad_get_current_caps (pad);
  if (caps == NULL) {
    return;
  }
  caps_str = gst_caps_to_string (caps);
  if (caps_str == NULL)
    return;
  g_print ("pad caps : \n%s\n", caps_str);
  if (g_strstr_len (caps_str, 5, "video")) {
    sinkpad = gst_element_get_static_pad (player->vsink, "sink");
    gst_pad_link (pad, sinkpad);
  } else if (g_strstr_len (caps_str, 5, "audio")) {
    sinkpad = gst_element_get_static_pad (player->asink, "sink");
    gst_pad_link (pad, sinkpad);
  }
  //todo: g_free(caps_str);
  gst_object_unref (sinkpad);
}

int
main (int argc, char **argv)
{

  GMainLoop *mainloop;
  GstElement *pipeline, *src, *decoder, *v_sink, *a_sink;
  GstBus *bus;
  GMainContext *context = NULL;
  gboolean is_running = FALSE;
  GIOChannel *io_channel;

  guint bus_watch_id;
  gint io_watch_id;
  player_t player;

  gst_init (&argc, &argv);

  mainloop = g_main_loop_new (context, is_running);

  if (argc != 2) {
    g_printerr ("Usage : %s <video file path>\n", argv[0]);
    return -1;
  }

  pipeline = gst_pipeline_new ("cli-player");
  src = gst_element_factory_make ("filesrc", "filesource0");
  decoder = gst_element_factory_make ("decodebin", "decodebin0");
  v_sink = gst_element_factory_make ("autovideosink", "vsink0");
  a_sink = gst_element_factory_make ("autoaudiosink", "asink0");
  if (!pipeline || !src || !decoder || !a_sink || !v_sink) {
    g_printerr ("failed to create one of the elements");
    return -1;
  }

  g_object_set (G_OBJECT (src), "location", argv[1], NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, cb_bus, mainloop);
  gst_object_unref (bus);
  player.asink = a_sink;
  player.vsink = v_sink;
  gst_bin_add_many (GST_BIN (pipeline), src, decoder, a_sink, v_sink, NULL);

  gst_element_link (src, decoder);
  g_signal_connect (decoder, "pad-added", G_CALLBACK (cb_pad_added), &player);

  struct termios curr_settings, new_settings;
  if (tcgetattr (STDIN_FILENO, &curr_settings) != 0) {
    g_print ("Failed to get current term attributes \n");
    return -1;
  }
  new_settings = curr_settings;
  new_settings.c_lflag &= ~(ICANON | IEXTEN);
  if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &new_settings)) {
    g_print ("Failed to set new term attributes \n");
    return -1;
  }
  io_channel = g_io_channel_unix_new (STDIN_FILENO);

  player.loop = mainloop;
  player.pipeline = pipeline;
  int flags;
  flags = g_io_channel_get_flags (io_channel);
  g_io_channel_set_flags (io_channel, flags | G_IO_FLAG_NONBLOCK, NULL);

  io_watch_id =
      g_io_add_watch (io_channel, G_IO_IN, (GIOFunc) cb_io_watch,
      (gpointer) & player);
  g_io_channel_unref (io_channel);
  print_menu ();

  g_timeout_add (200, (GSourceFunc) cb_print_position, pipeline);

  g_print ("Starting the pipeline ....\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_print ("Running mainloop...\n");
  g_main_loop_run (mainloop);

  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (mainloop);

  return 0;

}
