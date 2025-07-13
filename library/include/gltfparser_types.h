#ifndef GLTFPARSER_TYPES_INCLUDED
#define GLTFPARSER_TYPES_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GLTF 2.0 specification types
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#accessor-data-types
typedef enum {
    Type_Scalar,
    Type_Vec2,
    Type_Vec3,
    Type_Vec4,
    Type_Mat2,
    Type_Mat3,
    Type_Mat4
} GLTF_Type;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#accessor-data-types
typedef enum {
    ComponentType_R8            = 5120, // byte
    ComponentType_R8_UNSIGNED   = 5121, // unsigned short
    ComponentType_R16           = 5122, // short
    ComponentType_R16_UNSIGNED  = 5123, // unsigned short
    ComponentType_R32_UNSIGNED  = 5125, // unsigned int
    ComponentType_R32_FLOAT     = 5126  // float
} GLTF_ComponentType;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#buffers-and-buffer-views
typedef enum {
    BufferViewType_Invalid  = 0,
    BufferViewType_Vertex   = 34962,
    BufferViewType_Indices  = 34963
} GLTF_BufferViewType;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#animations
typedef enum {
    AnimationPathType_Invalid,
    AnimationPathType_Translation,
    AnimationPathType_Rotation,
    AnimationPathType_Scale,
    AnimationPathType_Weights
} GLTF_AnimationPathType;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-c-interpolation
typedef enum {
    InterpolationType_Linear,
    InterpolationType_Step,
    InterpolationType_CubicSpline
} GLTF_InterpolationType;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_camera_type
typedef enum {
    CameraType_Perspective,
    CameraType_Orthographic
} GLTF_CameraType;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_filtering
typedef enum {
    FilterType_Nearest              = 9728,
    FilterType_Linear               = 9729,
    FilterType_NearestMipmapNearest = 9984,
    FilterType_LinearMipmapNearest  = 9985,
    FilterType_NearestMipmapLinear  = 9986,
    FilterType_LinearMipmapLinear   = 9987
} GLTF_FilterType;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_wrapping
typedef enum {
    WrapMode_ClampToEdge    = 33071,
    WrapMode_MirroredRepeat = 33648,
    WrapMode_Repeat         = 10497
} GLTF_WrapMode;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#alpha-coverage
typedef enum {
    AlphaMode_Opaque,
    AlphaMode_Mask,
    AlphaMode_Blend
} GLTF_AlphaMode;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_mesh_primitive_mode
typedef enum {
    PrimitiveType_Points            = 0,
    PrimitiveType_Lines             = 1,
    PrimitiveType_LineLoop          = 2,
    PrimitiveType_LineStrip         = 3,
    PrimitiveType_Triangles         = 4,
    PrimitiveType_TriangleStrip     = 5,
    PrimitiveType_TriangleFan       = 6
} GLTF_PrimitiveType;

/// @brief GLTf 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_mesh_primitive_attributes
typedef enum {
    AttributeType_Invalid,
    AttributeType_Position,
    AttributeType_Normal,
    AttributeType_Tangent,
    AttributeType_TexCoord,
    AttributeType_Color,
    AttributeType_Joints,
    AttributeType_Weights,
    AttributeType_Custom
} GLTF_AttributeType;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GLTF 2.0 specification structs
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// @brief forward declaration
typedef struct GLTF_Node GLTF_Node;

/// @brief information about a extension
typedef struct {
    char* name;
    char* data;
} GLTF_Extension;

/// GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#asset
typedef struct {
    char* copyright;
    char* generator;
    char* version;
    char* minVersion;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_Asset;

// GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#buffers-and-buffer-views
typedef struct {
    void* data; // loaded uppon load buffer
    char* name;
    unsigned long long size;
    char* URI;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_Buffer;

/// GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#buffers-and-buffer-views
typedef struct {
    char* name;
    GLTF_BufferViewType type;
    GLTF_Buffer* buffer;
    unsigned long long offset;
    unsigned long long size;
    unsigned long long stride;
    void* data;
    char* extras;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
} GLTF_BufferView;

// GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#sparse-accessors
typedef struct {
    unsigned long long count;
    GLTF_BufferView* indicesBufferView;
    unsigned long long indicesByteOffset;
    GLTF_ComponentType indicesComponentType;
    GLTF_BufferView* valuesBufferView;
    unsigned long long valueByteOffset;
} GLTF_SparseAccessor;

/// GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#accessors
typedef struct {
    char* name;
    GLTF_ComponentType componentType;
    int normalized;
    GLTF_Type type;
    unsigned long long offset;
    unsigned long long count;
    unsigned long long stride;
    GLTF_BufferView* bufferView;
    int hasMin;
    int hasMax;
    float min[16];
    float max[16];
    int isSparse;
    GLTF_SparseAccessor sparse;
    char* extras;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
} GLTF_Accessor;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_mesh_primitive_attributes
typedef struct {
    char* name;
    GLTF_AttributeType type;
    int index;
    GLTF_Accessor* data;
} GLTF_Attribute;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_mesh_primitive_targets
typedef struct {
    unsigned long long attributesCount;
    GLTF_Attribute* attributes;
} GLTF_MorphTarget;

/// GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_animation_samplers
typedef struct {
    GLTF_Accessor* input;
    GLTF_Accessor* output;
    GLTF_InterpolationType interpolation;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_AnimationSampler;

/// GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_animation_channels
typedef struct {
    GLTF_AnimationSampler* sampler;
    GLTF_Node* targetNode;
    GLTF_AnimationPathType targetPath;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_AnimationChannel;

/// GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-animation
typedef struct {
    char* name;
    GLTF_AnimationSampler* samplers;
    unsigned long long samplersCount;
    GLTF_AnimationChannel* channels;
    unsigned long long channelsCount;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_Animation;

/// GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_camera_perspective
typedef struct {
    int hasAspectRatio;
    float aspectRatio;
    float yFOV;
    int hasZFar;
    float zFar;
    float zNear;
    char* extras;
} GLTF_CameraPerspective;

/// GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_camera_orthographic
typedef struct {
    int padding;
    float xMag;
    float yMag;
    float zFar;
    float zNear;
    char* extras;
} GLTF_CameraOrthographic;

/// GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-camera
typedef struct {
    char* name;
    GLTF_CameraType type;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    union {
        GLTF_CameraPerspective perspective;
        GLTF_CameraOrthographic orthographic;
    } data;
    char* extras;
} GLTF_Camera;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#images
typedef struct {
    char* name;
    char* URI;
    GLTF_BufferView* bufferView;
    char* mime_type;
    char* extras;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
} GLTF_Image;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#samplers
typedef struct {
    char* name;
    GLTF_FilterType magFilter;
    GLTF_FilterType minFilter;
    GLTF_WrapMode wrapS;
    GLTF_WrapMode wrapT;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_ImageSampler;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#textures
typedef struct {
    char* name;
    GLTF_Image* image;
    GLTF_ImageSampler* sampler;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_Texture;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#texture-data
typedef struct {
    GLTF_Texture* texture;
    int texCoord;
    float scale;
    float strength;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
} GLTF_TextureView;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#metallic-roughness-material
typedef struct {
    GLTF_TextureView baseColorTexture;
    GLTF_TextureView metallicRoughnessTexture;
    float baseColor[4];
    float metallicFactor;
    float roughnessFactor;
} GLTF_PBRMetallicRoughness;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials
typedef struct {
    char* name;
    GLTF_PBRMetallicRoughness PBRmetallicRoughness;
    GLTF_TextureView normalTexture;
    GLTF_TextureView occlusionTexture;
    GLTF_TextureView emissiveTexture;
    float emissiveFactor[3];
    GLTF_AlphaMode alphaMode;
    float alphaCutoff;
    int doubleSided;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_Material;

/// @brief
typedef struct {
    unsigned long long variant;
    GLTF_Material* material;
    char* extras;
} GLTF_MaterialMapping;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-mesh-primitive
typedef struct {

    GLTF_PrimitiveType type;
    GLTF_Accessor* indices;
    GLTF_Material* material;
    unsigned long long attributesCount;
    GLTF_Attribute* attributes;
    unsigned long long targetsCount;
    GLTF_MorphTarget* targets;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_Primitive;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes
typedef struct {
    char* name;
    unsigned long long primitivesCount;
    GLTF_Primitive* primitives;
    unsigned long long weightsCount;
    float* weights;
    unsigned long long targetNamesCount;
    char** targetNames;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_Mesh;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-skin
typedef struct {
    char* name;
    unsigned long long jointsCount;
    GLTF_Node** joints;
    GLTF_Node* skeleton;
    GLTF_Accessor* inverseBindMatrices;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_Skin;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#scenes
typedef struct {
    char* name;
    unsigned long long nodesCount;
    GLTF_Node** nodes;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
} GLTF_Scene;

/// @brief GLTF 2.0 specification https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-node
struct GLTF_Node {
    char* name;
    GLTF_Node* parent;
    unsigned long long childrenCount;
    GLTF_Node** children;
    GLTF_Skin* skin;
    GLTF_Mesh* mesh;
    GLTF_Camera* camera;
    unsigned long long weightsCount;
    float* weights;
    float translation[3];
    float rotation[4];
    float scale[3];
    float matrix[16];
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char* extras;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// API structs
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    const char* path;                   // the path on disk
    void* bin;                          // glb bin data, if applicable
    unsigned long long binSize;                     // glb bin size, if applicable
    int type;                           // 1: GLFW file 0: GLB file
    unsigned int jsonTkCount;           // holds how many tokens the json has
    const char* json;                   // json data, for latter reference
    unsigned long long jsonSize;                    // json data size, for latter reference
} GLTF_FileInfo;

/// @brief final structure for the parsed data
typedef struct {
    GLTF_FileInfo fileInfo;
    GLTF_Asset asset;
    unsigned long long accessorsCount;
    GLTF_Accessor* accessors;
    unsigned long long bufferViewsCount;
    GLTF_BufferView* bufferViews;
    unsigned long long buffersCount;
    GLTF_Buffer* buffers;
    unsigned long long animationsCount;
    GLTF_Animation* animations;
    unsigned long long camerasCount;
    GLTF_Camera* cameras;
    unsigned long long imagesCount;
    GLTF_Image* images;
    unsigned long long materialsCount;
    GLTF_Material* materials;
    unsigned long long meshesCount;
    GLTF_Mesh* meshes;
    unsigned long long nodesCount;
    GLTF_Node* nodes;
    unsigned long long imageSamplersCount;
    GLTF_ImageSampler* imageSamplers;
    unsigned long long scenesCount;
    GLTF_Scene* scenes;
    GLTF_Scene* scene;
    unsigned long long texturesCount;
    GLTF_Texture* textures;
    unsigned long long skinsCount;
    GLTF_Skin* skins;
    unsigned long long extensionsCount;
    GLTF_Extension* extensions;
    char** extensionsUsed;
    unsigned long long extensionsUsedCount;
    char** extensionsRequired;
    unsigned long long extensionsRequiredCount;
    char* extras;
} GLTF2;

#ifdef __cplusplus
}
#endif

#endif // GLTFPARSER_TYPES_INCLUDED