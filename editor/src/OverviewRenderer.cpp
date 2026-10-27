#include "OverviewRenderer.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>

namespace {

// Structural grid: the Cell lattice the Deck is drawn on.
constexpr std::uint8_t kGridRed = 70;
constexpr std::uint8_t kGridGreen = 78;
constexpr std::uint8_t kGridBlue = 90;
// Filled Cell: a floor plate. Void Cells draw nothing, leaving open space.
constexpr std::uint8_t kFloorRed = 150;
constexpr std::uint8_t kFloorGreen = 200;
constexpr std::uint8_t kFloorBlue = 235;

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
    const station::editor::SpaceStation& station) {
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
    mGrid->setLines(buildSceneLines(station, width, height));
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

std::vector<OverviewRenderer::Line> OverviewRenderer::buildSceneLines(
    const station::editor::SpaceStation& station, std::size_t width,
    std::size_t height) {
    std::vector<Line> lines;
    if (station.deckCount() == 0) {
        return lines;
    }
    const station::editor::Deck& deck = station.deck(0);

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float cols =
        static_cast<float>(std::max<std::uint32_t>(1, deck.width()));
    const float rows =
        static_cast<float>(std::max<std::uint32_t>(1, deck.height()));
    const float cell = std::max(1.0f, std::floor(std::min(w / cols, h / rows)));
    const float grid_width = cell * cols;
    const float grid_height = cell * rows;
    const float offset_x = (w - grid_width) * 0.5f;
    const float offset_y = (h - grid_height) * 0.5f;

    const std::array<std::uint8_t, 4> grid_colour = {
        kGridRed, kGridGreen, kGridBlue, 255};
    const std::array<std::uint8_t, 4> floor_colour = {
        kFloorRed, kFloorGreen, kFloorBlue, 255};
    lines.reserve(2 * (static_cast<std::size_t>(deck.width() + deck.height()) + 2) +
                  4 * deck.filledCount());
    for (std::uint32_t i = 0; i <= deck.width(); ++i) {
        const float x = offset_x + cell * static_cast<float>(i);
        lines.push_back({x, offset_y, x, offset_y + grid_height,
                        grid_colour[0], grid_colour[1], grid_colour[2],
                        grid_colour[3]});
    }
    for (std::uint32_t i = 0; i <= deck.height(); ++i) {
        const float y = offset_y + cell * static_cast<float>(i);
        lines.push_back({offset_x, y, offset_x + grid_width, y,
                        grid_colour[0], grid_colour[1], grid_colour[2],
                        grid_colour[3]});
    }

    for (std::uint32_t y = 0; y < deck.height(); ++y) {
        for (std::uint32_t x = 0; x < deck.width(); ++x) {
            if (deck.cell(x, y) != station::editor::CellState::Filled) {
                continue;
            }
            const float left = offset_x + cell * static_cast<float>(x);
            const float top = offset_y + cell * static_cast<float>(y);
            const float right = left + cell;
            const float bottom = top + cell;
            lines.push_back({left, top, right, top,
                           floor_colour[0], floor_colour[1], floor_colour[2],
                           floor_colour[3]});
            lines.push_back({right, top, right, bottom,
                           floor_colour[0], floor_colour[1], floor_colour[2],
                           floor_colour[3]});
            lines.push_back({right, bottom, left, bottom,
                           floor_colour[0], floor_colour[1], floor_colour[2],
                           floor_colour[3]});
            lines.push_back({left, bottom, left, top,
                           floor_colour[0], floor_colour[1], floor_colour[2],
                           floor_colour[3]});
        }
    }
    return lines;
}
