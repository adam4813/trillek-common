#ifndef GRAPHICS_HPP_INCLUDED
#define GRAPHICS_HPP_INCLUDED

#include "opengl.hpp"

#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <Rocket/Core/RenderInterface.h>

#include <list>
#include <memory>
#include <vector>
#include <future>
#include <iostream>
#include "trillek.hpp"
#include "type-id.hpp"
#include "trillek-scheduler.hpp"
#include "component-factory.hpp"
#include "systems/system-base.hpp"
#include "util/json-parser.hpp"
#include "graphics/graphics-base.hpp"
#include "graphics/material.hpp"
#include "graphics/render-layer.hpp"
#include "graphics/texture.hpp"
#include "graphics/vertex-list.hpp"
#include <map>
#include "systems/dispatcher.hpp"
#include "os.hpp"

namespace trillek {

class Transform;

namespace gui {
class GuiSystem;
}
namespace graphics {

enum class RenderCmd : unsigned int;
class RenderCommandItem;
class Renderable;
class CameraBase;
class Animation;
class LightBase;
class RenderList;

struct MaterialGroup {
    Material material;
    struct TextureGroup {
        std::vector<size_t> texture_indicies;
        struct RenderableGroup {
            std::shared_ptr<Renderable> renderable;
            std::map<id_t, std::shared_ptr<Animation>> animations;
            std::list<id_t> instances;
            size_t buffer_group_index;
        };
        std::list<RenderableGroup> renderable_groups;
    };
    std::list<TextureGroup> texture_groups;
};

struct GUIVertex {
    float x, y;
    float ts, tt;
    uint8_t c[4];
};

struct VertexListEntry {
    uint32_t indexcount;
    uint32_t vertexcount;
    uint32_t textureref;
    uint32_t offset;
};

struct RenderEntry {
    uint32_t mode;
    uint32_t entryref;
    uint32_t extension;
};

class RenderSystem : public SystemBase, public util::Parser,
    public event::Subscriber<KeyboardEvent>
{
public:

    RenderSystem();

    // Inherited from Parser
    virtual bool Serialize(rapidjson::Document& document);

    // Inherited from Parser
    virtual bool Parse(rapidjson::Value& node);

    /**
     * \brief Starts the OpenGL rendering system.
     *
     * Prepares the OpengGL rendering context enabling and disabling certain flags.
     * \param const unsigned int width Initial viewport width.
     * \param const unsigned int height Initial viewport height.
     * \return const int*[2] -1 in the 0 index on failure, else the major and minor version in index 0 and 1 respectively.
     */
    const int* Start(const unsigned int width, const unsigned int height);

    /** \brief Makes the context of the window current to the thread
     */
    void ThreadInit() override;

    /** \brief Renders all the passes for a scene and updates screen.
     */
    void RenderScene() const;

    /** \brief Renders all textured geometry for the scene.
     */
    void RenderColorPass(const float *viewmatrix, const float *projmatrix) const;

    /** \brief Renders all geometry for the scene, but only the depth channel.
     */
    void RenderDepthOnlyPass(const float *view_matrix, const float *proj_matrix) const;

    /** \brief Renders all deferred lighting passes for the scene.
     */
    void RenderLightingPass(const glm::mat4x4 &view_matrix, const float *inv_proj_matrix) const;

    /** \brief Renders post processing passes for the scene.
     */
    void RenderPostPass(std::shared_ptr<Shader>) const;

    void RenderGUI() const;

    /**
     * \brief Causes an update in the system based on the change in time.
     *
     * Updates the state of the system based off how much time has elapsed since the last update.
     */
    void RunBatch() const override;

    /**
     * \brief Sets the viewport width and height.
     *
     * \param const unsigned int width New viewport width
     * \param const unsigned int height New viewport height
     */
    void SetViewportSize(const unsigned int width, const unsigned int height);

    /**
     * \brief Template for adding components to the system.
     *
     * This function is meant for specializations for each component type
     * \param const unsigned int The entity ID the component belongs to.
     * \param std::shared_ptr<typename CT> The component to add.
     * \return bool false if the component exists on the entity
     */
    template<typename CT>
    bool AddEntityComponent(const id_t entity_id, std::shared_ptr<CT>) {
        return false;
    }

    /**
     * \brief Adds a component to the system.
     *
     * If the component is not supported the method returns without adding the component.
     * \param const unsigned int The entity ID the component belongs to.
     * \param std::shared_ptr<ComponentBase> component to add.
     */
    void AddComponent(const id_t entity_id, std::shared_ptr<ComponentBase> component);

    /**
     * \brief Removes a Renderable component from the system..
     *
     * \param const unsigned int entityID The component to remove.
     */
    void RemoveRenderable(const unsigned int entity_id);

    /** \brief Handle incoming events to update data
     *
     * This function is called once every frame. It is the only
     * function that can write data. This function is in the critical
     * path, job done here must be simple.
     *
     * If event handling need some batch processing, a task list must be
     * prepared and stored temporarily to be retrieved by RunBatch().
     */
    void HandleEvents(const frame_tp& timepoint) override;

    /** \brief Save the data and terminate the system
     *
     * This function is called when the program is closing
     */
    void Terminate() override;

    /**
     * \brief Registers all graphics system tables required for operation and parsing.
     *
     * This function is defined in a separate source file to reduce compile times.
     * Internally it calls the templated RegisterSomething functions.
     */
    void RegisterTypes();

    /**
     * \brief Register a function to instance and parse a graphics class
     */
    template<class RT>
    void RegisterClassGenParser() {
        RenderSystem &rensys = *this;
        auto cgenlambda =  [&rensys] (const rapidjson::Value& node) -> bool {
            if(!node.IsObject()) {
                // TODO use logger
                std::cerr << "[ERROR] Invalid type for " << reflection::GetTypeName<RT>() << "\n";
                return false;
            }
            for(auto section_itr = node.MemberBegin();
                    section_itr != node.MemberEnd(); section_itr++) {
                std::string obj_name(section_itr->name.GetString(), section_itr->name.GetStringLength());
                std::shared_ptr<RT> objgen_ptr(new RT);
                if(objgen_ptr->Parse(obj_name, section_itr->value)) {
                    rensys.Add(obj_name, objgen_ptr);
                }
            }
            return true;
        };
        parser_functions[reflection::GetTypeName<RT>()] = cgenlambda;
    }

    void RegisterStaticParsers();
    void RegisterListResolvers();

    template<class T>
    std::shared_ptr<T> Get(const std::string & instancename) const {
        unsigned int type_id = reflection::GetTypeID<T>();
        auto typedmap = this->graphics_instances.find(type_id);
        if(typedmap == this->graphics_instances.end()) {
            return std::shared_ptr<T>();
        }
        auto instance_ptr = typedmap->second.find(instancename);
        if(instance_ptr == typedmap->second.end()) {
            return std::shared_ptr<T>();
        }
        return std::static_pointer_cast<T>(instance_ptr->second);
    }
    template<class T>
    std::shared_ptr<T> Get(uint32_t instanceid) const {
        auto instance_ptr = this->graphics_references.find(instanceid);
        if(instance_ptr == this->graphics_references.end()) {
            return std::shared_ptr<T>();
        }
        return std::static_pointer_cast<T>(instance_ptr->second);
    }
    /**
     * \brief Adds a graphics object to the system.
     */
    template<typename T>
    void Add(const std::string & instancename, std::shared_ptr<T> instanceptr) {
        unsigned int type_id = reflection::GetTypeID<T>();
        graphics_instances[type_id][instancename] = instanceptr;
    }
    /**
     * \brief Adds a graphics object to the system.
     */
    uint32_t Add(std::shared_ptr<GraphicsBase> instanceptr) {
        uint32_t obj_id = current_ref++;
        if(!obj_id) obj_id = current_ref++; // must not be zero
        graphics_references[obj_id] = instanceptr;
        return obj_id;
    }
    /**
     * \brief Adds a texture object to the system.
     */
    uint32_t Add(std::shared_ptr<Texture> instanceptr) {
        uint32_t obj_id = current_ref++;
        dyn_textures.push_back(instanceptr);
        if(!obj_id) obj_id = current_ref++; // must not be zero
        graphics_references[obj_id] = instanceptr;
        return obj_id;
    }
    void Remove(uint32_t instanceid) {
        auto instance_ptr = this->graphics_references.find(instanceid);
        if(instance_ptr != this->graphics_references.end()) {
            this->graphics_references.erase(instance_ptr);
        }
    }

    struct BufferTri {
        BufferTri() : vao(0), vbo(0), ibo(0) { }
        GLuint vao;
        GLuint vbo;
        GLuint ibo;
    };
    struct ViewMatrixSet {
        ViewRect viewport;
        glm::mat4 projection_matrix;
        glm::mat4 view_matrix;
    };

    // returns an entity ID
    id_t GetActiveCameraID() const { return camera_id; }

    void Notify(const KeyboardEvent* key_event) {
        switch(key_event->action) {
        case KeyboardEvent::KEY_DOWN:
            switch (key_event->key) {
            case GLFW_KEY_F10:
                debugmode = (debugmode & ~3) | ((debugmode + 1) & 3);
                break;
            }
            break;
        default:
            break;
        }
    }

    /**
     * \brief Graphics interface for libRocket
     * The GuiRenderInterface class provides all the methods for libRocket to
     * render and generate graphics objects.
     * see Rocket/Core/RenderInterface.h for a description of the methods use.
     */
    class GuiRenderInterface : public Rocket::Core::RenderInterface {
        friend class RenderSystem;
    public:
        GuiRenderInterface(RenderSystem *);
        virtual ~GuiRenderInterface();

        virtual void RenderGeometry(
            Rocket::Core::Vertex* vertices,
            int num_vertices,
            int* indices,
            int num_indices,
            Rocket::Core::TextureHandle texture,
            const Rocket::Core::Vector2f& translation);
        virtual Rocket::Core::CompiledGeometryHandle CompileGeometry(
            Rocket::Core::Vertex* vertices,
            int num_vertices,
            int* indices,
            int num_indices,
            Rocket::Core::TextureHandle texture);
        virtual void RenderCompiledGeometry(
            Rocket::Core::CompiledGeometryHandle geometry,
            const Rocket::Core::Vector2f& translation);
        virtual void ReleaseCompiledGeometry(
            Rocket::Core::CompiledGeometryHandle geometry);
        virtual void EnableScissorRegion(bool enable);
        virtual void SetScissorRegion(int x, int y, int width, int height);
        virtual bool LoadTexture(
            Rocket::Core::TextureHandle& texture_handle,
            Rocket::Core::Vector2i& texture_dimensions,
            const Rocket::Core::String& source);
        virtual bool GenerateTexture(
            Rocket::Core::TextureHandle& texture_handle,
            const Rocket::Core::byte* source,
            const Rocket::Core::Vector2i& source_dimensions);
        virtual void ReleaseTexture(Rocket::Core::TextureHandle texture);

        void CheckReload();
        void CheckClear();
        void RequestClear() { reload_all = true; }
    private:
        RenderSystem *system;
        bool reload_vert;
        bool reload_index;
        uint32_t vertlistid;
        bool reload_all;
        std::vector<GUIVertex> renderverts;
        std::vector<uint32_t> renderindices;
        std::vector<VertexListEntry> vertlist;
        std::vector<glm::vec2> offsets;
        std::vector<RenderEntry> gui_renderset;
    };
    GuiRenderInterface * GetGUIInterface() {
        return gui_interface.get();
    }
private:

    template<class CT>
    int TryAddComponent(const id_t entity_id, std::shared_ptr<ComponentBase> comp) {
        if(reflection::GetTypeID<CT>() == comp->component_type_id) {
            auto ccomp = std::static_pointer_cast<CT>(comp);
            if (!ccomp) {
                return -1;
            }
            if(!AddEntityComponent(entity_id, ccomp)) {
                return -1;
            }
            return 1;
        }
        else {
            return 0;
        }
    }

    void UpdateModelMatrices();

    int gl_version[3];
    int debugmode;
    bool frame_drop;
    uint32_t frame_drop_count;
    ViewMatrixSet vp_center;
    ViewMatrixSet vp_left;
    ViewMatrixSet vp_right;
    //glm::mat4 projection_matrix;
    //glm::mat4 view_matrix;
    VertexList screen_quad;
    //BufferTri screenquad; /// the full screen quad, used for much graphics effects

    unsigned int window_width; // Store the width of our window
    unsigned int window_height; // Store the height of our window
    bool multisample;

    std::map<std::string, std::function<bool(const rapidjson::Value&)>> parser_functions;

    // A list of the renderables in the system. Stored as a pair (entity ID, Renderable).
    std::list<std::pair<id_t, std::shared_ptr<Renderable>>> renderables;

    // A list of the lights in the system. Stored as a pair (entity ID, LightBase).
    std::list<std::pair<id_t, std::shared_ptr<LightBase>>> alllights;

    // A list of all dynamic textures in the system
    std::list<std::shared_ptr<Texture>> dyn_textures;
    // A list of all textures that need to be removed from dyn_textures
    std::unique_ptr<std::list<std::shared_ptr<Texture>>> rem_textures;

    // map IDs to cameras
    std::map<id_t, std::shared_ptr<CameraBase>> cameras;

    // Active objects
    std::shared_ptr<RenderList> activerender;
    std::shared_ptr<Shader> lightingshader;
    std::shared_ptr<Shader> depthpassshader;
    std::shared_ptr<Shader> guisysshader;
    std::shared_ptr<CameraBase> camera;
    id_t camera_id;

    std::unique_ptr<GuiRenderInterface> gui_interface;

    uint32_t current_ref;

    std::map<RenderCmd, std::function<bool(RenderCommandItem&)>> list_resolvers;

    std::map<uint32_t, std::shared_ptr<GraphicsBase>> graphics_references;
    std::map<unsigned int, std::map<std::string, std::shared_ptr<GraphicsBase>>> graphics_instances;
    std::map<unsigned int, glm::mat4> model_matrices;
    std::list<MaterialGroup> material_groups;
    std::shared_future<std::shared_ptr<const std::map<unsigned int,const Transform*>>> updated_transforms;
};

/**
 * \brief Adds a graphics Texture to the system.
 */
template<>
void RenderSystem::Add(const std::string & instancename, std::shared_ptr<Texture> instanceptr);

/**
 * \brief Adds a renderable component to the system.
 */
template<>
bool RenderSystem::AddEntityComponent(const id_t entity_id, std::shared_ptr<Renderable>);

/**
 * \brief Adds a light component to the system.
 */
template<>
bool RenderSystem::AddEntityComponent(const id_t entity_id, std::shared_ptr<LightBase>);

template<>
bool RenderSystem::AddEntityComponent(const id_t entity_id, std::shared_ptr<CameraBase>);


} // End of graphics

namespace reflection {
TRILLEK_MAKE_IDTYPE(graphics::RenderSystem, 400)
} // namespace reflection

} // End of trillek

#endif
