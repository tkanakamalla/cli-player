#include <gst/gst.h>
#include <glib.h>

static gboolean
cb_bus (GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
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

static void
cb_pad_added (GstElement *element, GstPad *pad, gpointer data) {
    GstPad *sinkpad;
    GstElement *sink = (GstElement *) data;

    g_print ("Dynamic pad created link decoder and sink\n");
    sinkpad = gst_element_get_static_pad (sink, "sink");
    gst_pad_link(pad, sinkpad);

    gst_object_unref(sinkpad);    
}

int main(int argc, char **argv) {
    
    GMainLoop *mainloop;
    GstElement *pipeline, *src, *decoder, *sink;
    GstBus *bus;
    GMainContext *context = NULL;
    gboolean is_running = FALSE;

    guint bus_watch_id;

    gst_init (&argc, &argv);

    mainloop = g_main_loop_new (context, is_running);

    if (argc != 2) {
        g_printerr ("Usage : %s <video file path>\n", argv[0]);
        return -1;
    }

    pipeline = gst_pipeline_new ("cli-player");
    src = gst_element_factory_make ("filesrc", "filesource0");
    decoder = gst_element_factory_make ("decodebin", "decodebin0");
    sink = gst_element_factory_make ("autovideosink", "sink0");

    if (!pipeline || !src || !decoder || !sink) {
        g_printerr ("failed to create one of the elements");
        return -1;
    }

    g_object_set (G_OBJECT (src),"location", argv[1], NULL);
    
    bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    bus_watch_id = gst_bus_add_watch (bus, cb_bus, mainloop);
    gst_object_unref (bus);

    gst_bin_add_many (GST_BIN(pipeline), src, decoder, sink, NULL);
    
    gst_element_link (src, decoder);
      g_signal_connect (decoder, "pad-added", G_CALLBACK (cb_pad_added), sink);

    g_print("Starting the pipeline ....\n");
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
    g_print ("Running mainloop...\n");
    g_main_loop_run(mainloop);

    g_print ("Returned, stopping playback\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);

    g_print ("Deleting pipeline\n");
    gst_object_unref (GST_OBJECT (pipeline));
    g_source_remove (bus_watch_id);
    g_main_loop_unref (mainloop);

    return 0;



}