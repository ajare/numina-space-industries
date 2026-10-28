// MPP-backed renderer for the Overview tab. Renders offscreen into a texture
// presented by ImGui. Shut down while the creating OpenGL context is current.
#pragma once

#include <mpp/Logger.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/RenderTexture.h>
#include <mpp/Scene.h>

#include "station/editor/Installation.h"

#include <cstddef>
#include <cstdint>
#include <memory>

class OverviewRenderer {
public:
    // Call after the OpenGL context is current.
    void initialise(std::size_t width, std::size_t height);

    // Solid floor tiles for the selected Level; Void remains background.
    std::uint32_t render(std::size_t width, std::size_t height,
                         const station::editor::Installation& station,
                         std::size_t level = 0);

    // Releases all GPU resources. Must run before the GL context dies.
    void shutdown();

private:
    std::unique_ptr<mpp::Logger> mMppLogger;
    std::unique_ptr<mpp::RenderSystem> mRenderSystem;
    std::unique_ptr<mpp::ResourceManager> mRenderResources;
    mpp::ScenePtr mScene;
    mpp::RenderTargetPtr mTarget;
};
