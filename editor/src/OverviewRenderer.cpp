#include "OverviewRenderer.h"
#include "OverviewGeometry.h"

#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace {

mpp::Colour clearColour() {
    return {0.08f, 0.08f, 0.08f, 1.0f};
}

}  // namespace

void OverviewRenderer::initialise(std::size_t width, std::size_t height) {
    const std::filesystem::path logDir(EDITOR_MPP_LOG_DIR);
    std::filesystem::create_directories(logDir);

    mMppLogger = std::make_unique<mpp::Logger>();
    if (!mMppLogger->initialise((logDir / "editor-mpp.log").string(),
                               mpp::Logger::Level::Debug)) {
        throw std::runtime_error("Could not initialise the MPP logger");
    }

    mRenderSystem = std::make_unique<mpp::RenderSystem>(width, height,
                                                       mMppLogger.get());
    mRenderResources = std::make_unique<mpp::ResourceManager>(
        mRenderSystem.get(), mMppLogger.get());
    mRenderSystem->createCoreResources(mRenderResources.get());

    mScene = mRenderSystem->createScene("Default");
    mScene->load();
    mScene->setClearColour(clearColour());
}

std::uint32_t OverviewRenderer::render(
    std::size_t width, std::size_t height,
    const station::editor::Installation& station, std::size_t level) {
    // MPP's built-in quad mesh follows its display dimensions, so keep it
    // aligned with the offscreen target when the Overview panel is resized.
    if (mRenderSystem->getWindowWidth() != width ||
        mRenderSystem->getWindowHeight() != height) {
        mRenderSystem->setDisplay(static_cast<int>(width), static_cast<int>(height));
    }
    if (!mTarget) {
        mTarget = mRenderSystem->createRenderTexture("Overview.Target", width,
                                                    height, 1, false);
    } else if (mTarget->getWidth() != width || mTarget->getHeight() != height) {
        static_cast<mpp::RenderTexture*>(mTarget.get())->resize(width, height);
    }
    mScene->setViewport(0, 0, width, height);

    mRenderSystem->pushRenderTarget(mTarget);
    mRenderSystem->setViewport(0, 0, width, height);
    mRenderSystem->clearScreen(clearColour());
    mRenderSystem->setProjection2dOrthographic();
    mRenderSystem->resetTransform();
    const mpp::Colour floor_colour{150.0f / 255.0f, 200.0f / 255.0f,
                                   235.0f / 255.0f, 1.0f};
    for (const auto& cell : buildOverviewCells(station, width, height, level)) {
        // Round shared edges identically so adjacent floor tiles have no seams.
        const int left = static_cast<int>(std::lround(cell.x - cell.halfSize));
        const int top = static_cast<int>(std::lround(cell.y - cell.halfSize));
        const int right = static_cast<int>(std::lround(cell.x + cell.halfSize));
        const int bottom = static_cast<int>(std::lround(cell.y + cell.halfSize));
        if (right > left && bottom > top) {
            // MPP anchors quads at their lower edge in top-down coordinates.
            mRenderSystem->renderQuad(left, bottom, right - left, bottom - top,
                                      floor_colour, false, false);
        }
    }
    mRenderSystem->flushVertexBuffers();
    mRenderSystem->popRenderTarget();

    return static_cast<mpp::RenderTexture*>(mTarget.get())->getId();
}

void OverviewRenderer::shutdown() {
    mTarget.reset();
    if (mScene) {
        mScene->unload();
        mScene.reset();
    }
    if (mRenderSystem) {
        mRenderSystem->destroyCoreResources();
    }
    mRenderResources.reset();
    mRenderSystem.reset();
    mMppLogger.reset();
}
