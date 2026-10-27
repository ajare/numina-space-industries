#include "OverviewRenderer.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace {

constexpr int kGridCells = 16;

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

std::uint32_t OverviewRenderer::render(std::size_t width, std::size_t height) {
    if (!mTarget) {
        mTarget = mRenderSystem->createRenderTexture("Overview.Target", width,
                                                    height, 1, false);
    } else if (mTarget->getWidth() != width || mTarget->getHeight() != height) {
        static_cast<mpp::RenderTexture*>(mTarget.get())->resize(width, height);
    }
    mScene->setViewport(0, 0, width, height);

    if (!mLineRenderer) {
        mGrid = std::make_shared<GridLines>();
        const mpp::helper::LineBatchRendererParams params{false, true, false};
        mLineRenderer = std::make_shared<
            mpp::helper::LineBatchRenderer<Float, Byte>>(
            "Overview.Grid", params, mGrid, mRenderSystem.get(),
            mRenderResources.get());
        mLineRenderer->create();
        mSceneModel = mScene->add2dBatch(mGrid, mLineRenderer, 0);
        mSceneModel->getParams()->setModelBlend(true);
    }
    mGrid->setLines(buildGridLines(width, height));
    mLineRenderer->update();

    mRenderSystem->pushRenderTarget(mTarget);
    mRenderSystem->setViewport(0, 0, width, height);
    mRenderSystem->clearScreen(clearColour());
    mRenderSystem->setProjection2dOrthographic();
    mRenderSystem->resetTransform();
    mSceneModel->render({});
    mRenderSystem->flushVertexBuffers();
    mRenderSystem->popRenderTarget();

    return static_cast<mpp::RenderTexture*>(mTarget.get())->getId();
}

void OverviewRenderer::shutdown() {
    mSceneModel.reset();
    mLineRenderer.reset();
    mGrid.reset();
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

std::vector<OverviewRenderer::Line> OverviewRenderer::buildGridLines(
    std::size_t width, std::size_t height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float cell = std::max(1.0f, std::floor(std::min(w, h) / kGridCells));
    const float gridSize = cell * kGridCells;
    const float offsetX = (w - gridSize) * 0.5f;
    const float offsetY = (h - gridSize) * 0.5f;

    std::vector<Line> lines;
    lines.reserve(2 * static_cast<std::size_t>(kGridCells + 1));
    for (int i = 0; i <= kGridCells; ++i) {
        const float x = offsetX + cell * static_cast<float>(i);
        lines.push_back({x, offsetY, x, offsetY + gridSize});
        const float y = offsetY + cell * static_cast<float>(i);
        lines.push_back({offsetX, y, offsetX + gridSize, y});
    }
    return lines;
}
