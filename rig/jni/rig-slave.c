#include <config.h>

#include <glib.h>
#include <rut.h>
#include <rig-engine.h>
#include <rig-engine.h>
#include <rig-avahi.h>
#include <rig-rpc-network.h>
#include <cogl-gst/cogl-gst.h>

#include "rig-pb.h"

#include "rig.pb-c.h"

#if 0
static const GOptionEntry rut_handset_entries[] =
{
  { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY,
    &_rig_preview_remaining_args, "Project" },
  { 0 }
};
#endif

typedef struct _RigSlave
{
  RigEngine *engine;

} RigSlave;

static void
slave__test (Rig__Slave_Service *service,
             const Rig__Query *query,
             Rig__TestResult_Closure closure,
             void *closure_data)
{
  Rig__TestResult result = RIG__TEST_RESULT__INIT;
  //RigSlave *slave = rig_pb_rpc_closure_get_connection_data (closure_data);

  g_return_if_fail (query != NULL);

  g_print ("Test Query\n");

  closure (&result, closure_data);
}

static void
slave__load_asset (Rig__Slave_Service *service,
                   const Rig__SerializedAsset *query,
                   Rig__LoadAssetResult_Closure closure,
                   void *closure_data)
{
  Rig__LoadAssetResult result = RIG__LOAD_ASSET_RESULT__INIT;
  RigSlave *slave = rig_pb_rpc_closure_get_connection_data (closure_data);
  RigEngine *engine = slave->engine;

  g_return_if_fail (query != NULL);

  if (query->has_type)
    {
      RutAsset *asset =
        rut_asset_new_from_data (engine->ctx,
                                 query->path,
                                 query->type,
                                 query->data.data,
                                 query->data.len);

      rig_register_asset (engine, asset);

      g_print ("Load Asset Request\n");
    }

  closure (&result, closure_data);
}

static void
slave__load (Rig__Slave_Service *service,
             const Rig__UI *ui,
             Rig__LoadResult_Closure closure,
             void *closure_data)
{
  Rig__LoadResult result = RIG__LOAD_RESULT__INIT;
  RigSlave *slave = rig_pb_rpc_closure_get_connection_data (closure_data);
  RigEngine *engine = slave->engine;

  g_return_if_fail (ui != NULL);

  g_print ("UI Load Request\n");

  rig_pb_unserialize_ui (engine, ui);

  rig_engine_set_onscreen_size (engine,
                                engine->device_width / 2,
                                engine->device_height / 2);

  closure (&result, closure_data);
}


static Rig__Slave_Service rig_slave_service =
  RIG__SLAVE__INIT(slave__);

static void
client_close_handler (PB_RPC_ServerConnection *conn,
                      void *user_data)
{
  g_warning ("slave master disconnected %p", conn);
}

static void
new_client_handler (PB_RPC_Server *server,
                    PB_RPC_ServerConnection *conn,
                    void *user_data)
{
  RigSlave *slave = user_data;
  //RigEngine *engine = slave->engine;

  rig_pb_rpc_server_connection_set_close_handler (conn,
                                                  client_close_handler,
                                                  slave);

  rig_pb_rpc_server_connection_set_data (conn, slave);

  g_message ("slave master connected %p", conn);
}

static void
server_error_handler (PB_RPC_Error_Code code,
                      const char *message,
                      void *user_data)
{
  RigSlave *slave = user_data;
  RigEngine *engine = slave->engine;

  g_warning ("Server error: %s", message);

  rig_rpc_stop_server (engine);
}

void
rig_slave_init (RutShell *shell, void *user_data)
{
  RigSlave *slave = user_data;
  RigEngine *engine = slave->engine;

  rig_rpc_start_server (engine,
                        &rig_slave_service.base,
                        server_error_handler,
                        new_client_handler,
                        slave);

  rig_engine_init (shell, slave->engine);
}

void
rig_slave_fini (RutShell *shell, void *user_data)
{
  RigSlave *slave = user_data;

  rig_engine_fini (shell, slave->engine);
}

CoglBool
rig_slave_paint (RutShell *shell, void *user_data)
{
  RigSlave *slave = user_data;

  return rig_engine_paint (slave->engine->shell, slave->engine);
}

#ifdef __ANDROID__

void
android_main (struct android_app *application)
{
  RigSlave slave;
  RigEngine engine;

  /* Make sure glue isn't stripped */
  app_dummy ();

  g_android_init ();

  memset (&slave, 0, sizeof (RigSlave));
  slave.engine = &engine;

  memset (&engine, 0, sizeof (RigEngine));
  engine.app = application;

  _rig_in_device_mode = TRUE;

  engine.shell = rut_android_shell_new (application,
                                      rig_slave_init,
                                      rig_slave_fini,
                                      rig_slave_paint,
                                      &slave);

  engine.ctx = rut_context_new (engine.shell);
  gst_init (&argc, &argv);

  rut_context_init (engine.ctx);

  rut_shell_set_input_callback (engine.shell,
                                rig_engine_input_handler,
                                &engine);

  rut_shell_main (engine.shell);
}

#else

int
main (int argc, char **argv)
{
  RigSlave slave;
  RigEngine engine;
  GOptionContext *context = g_option_context_new (NULL);
  GError *error = NULL;

  //g_option_context_add_main_entries (context, rut_handset_entries, NULL);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      fprintf (stderr, "option parsing failed: %s\n", error->message);
      exit (EXIT_FAILURE);
    }

  memset (&slave, 0, sizeof (RigSlave));
  slave.engine = &engine;

  memset (&engine, 0, sizeof (RigEngine));

  engine.shell = rut_shell_new (rig_slave_init,
                              rig_slave_fini,
                              rig_slave_paint,
                              &slave);

  engine.ctx = rut_context_new (engine.shell);
  gst_init (&argc, &argv);

  rut_context_init (engine.ctx);

  rut_shell_add_input_callback (engine.shell,
                                rig_engine_input_handler,
                                &engine, NULL);

  _rig_in_device_mode = TRUE;

  rut_shell_main (engine.shell);

  return 0;
}

#endif
