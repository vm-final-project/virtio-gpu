/* Provide a no-op `ggml_backend_amx_buffer_type()` so the registry built with
 * `GGML_USE_CPU=1` links cleanly without the upstream AMX backend. */
#include <ggml-backend.h>

extern "C" {
ggml_backend_buffer_type_t ggml_backend_amx_buffer_type(void)
{
    return nullptr;
}
}
