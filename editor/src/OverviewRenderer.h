// MPP-backed renderer for the Overview tab.
//
// Follows the sibling Promethium editor's pattern (its ADR 0010): MPP
// renders offscreen into a render texture and ImGui presents that texture
// as an image. This class owns the MPP logger, render system, render
// resources and scene, and must be shut down while the OpenGL context that
// created them is still current.
#pragma once

#include <mpp/Logger.h>
#include <mpp/RenderSystem.h>
#include <mpp/ResourceManager.h>
#include <mpp/RenderTexture.h>
#include <mpp/Scene.h>
#include <mpp/helper/LineBatchDataProvider.h>
#include <mpp/helper/LineBatchRenderer.h>
#include <mpp/mesh/VertexTypeSpecification.h>

#include "station/editor/SpaceStation.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class OverviewRenderer {
public:
    // Creates the MPP logger, render system, core resources and scene.
    // Call after the OpenGL context is current.
    void initialise(std::size_t width, std::size_t height);

    // Renders the station's current geometry into the offscreen target
    // (created or resized on demand) and returns the GL texture id for
    // ImGui::Image. The grid frame follows the Deck's own dimensions, so
    // the preview tracks what the Editor panel is editing.
    std::uint32_t render(std::size_t width, std::size_t height,
                        const station::editor::SpaceStation& station);

    // Releases all GPU resources. Must run before the GL context dies.
    void shutdown();

private:
    struct Line {
        float x0;
        float y0;
        float x1;
        float y1;
        std::uint8_t red;
        std::uint8_t green;
        std::uint8_t blue;
        std::uint8_t alpha;
    };

    using Float = mpp::mesh::DataTypeFloat;
    using Byte = mpp::mesh::DataTypeUnsignedByte;

    // Feeds the grid geometry to MPP's native line batch.
    class GridLines final
        : public mpp::helper::LineBatchDataProvider<Float, Byte> {
    public:
        void setLines(std::vector<Line> lines) {
            mLines = std::move(lines);
            setNumPrimitives(mLines.size());
        }

        void position(std::uint32_t index, float& x0, float& y0, float& x1,
                     float& y1) override {
            const Line& line = mLines.at(index);
            x0 = line.x0;
            y0 = line.y0;
            x1 = line.x1;
            y1 = line.y1;
        }

        void colour(std::uint32_t index, std::uint8_t& red, std::uint8_t& green,
                    std::uint8_t& blue, std::uint8_t& alpha) override {
            const Line& line = mLines.at(index);
            red = line.red;
            green = line.green;
            blue = line.blue;
            alpha = line.alpha;
        }

        mpp::Colour diffuse() override { return mpp::Colour::White; }

        void getBounds(glm::vec3& minimum, glm::vec3& maximum) override {
            minimum = glm::vec3(0.0f, 0.0f, 0.0f);
            maximum = glm::vec3(0.0f, 0.0f, 0.0f);
        }

    private:
        std::vector<Line> mLines;
    };

    // Grid frame plus one outlined plate per Filled Cell, in the station's
    // current states.
    static std::vector<Line> buildSceneLines(
        const station::editor::SpaceStation& station, std::size_t width,
        std::size_t height);

    std::unique_ptr<mpp::Logger> mMppLogger;
    std::unique_ptr<mpp::RenderSystem> mRenderSystem;
    std::unique_ptr<mpp::ResourceManager> mRenderResources;
    mpp::ScenePtr mScene;
    mpp::RenderTargetPtr mTarget;
    std::shared_ptr<GridLines> mGrid;
    std::shared_ptr<mpp::helper::LineBatchRenderer<Float, Byte>> mLineRenderer;
    mpp::SceneModel2dPtr mSceneModel;
};
