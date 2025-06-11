#include "rigel_app.h"

int main(int, char**) 
{
    rigel_app_specs app_specs = {
        .title = "Rigel Webcam Capture App",
        .render_api = renderer_api::vulkan
    };

    rigel_app app(std::move(app_specs));
    return app.run();
}
