#ifndef PLATFORM_H_
#define PLATFORM_H_

struct MappedMemory {
    void *data;     // Base pointer to mapped file data
    void *mapping;  // Opaque handle for the mapping (ex. Windows HANDLE)
};
struct Platform {
    struct MappedMemory (*map_file)(const char *filename);
    void (*unmap_file)(struct MappedMemory *mm);
    double (*current_time_ms)();
    void (*sleep_ms)(double ms);
};
/* MEMORY MAPPING MESH */
typedef struct {
    unsigned int vertexCount;
    unsigned int indexCount;
    unsigned int boneCount;
    unsigned int frameCount;
    unsigned int vertexArrayOffset;
    unsigned int indexArrayOffset;
    unsigned int boneFramesArrayOffset;
} MeshHeader;
struct MappedMemory load_mesh(struct Platform *p, const char *filename, void** v, int *vc, void** i, int *ic) {
    struct MappedMemory mm = p->map_file(filename);
    
    MeshHeader *header = (MeshHeader*)mm.data;
    // Set pointers into the mapped memory using the header's offsets
    *vc = header->vertexCount;
    *v = (unsigned char*)mm.data + header->vertexArrayOffset;
    *ic  = header->indexCount;
    *i  = (unsigned int*)((unsigned char*)mm.data + header->indexArrayOffset);
    
    return mm;
}
struct MappedMemory load_animated_mesh(struct Platform *p, const char *filename,
                                   void** vertices, int *vertexCount,
                                   void** indices, int *indexCount,
                                   void** boneFrames, int *boneCount,
                                   int *frameCount) {
    // Map the file into memory.
    struct MappedMemory mm = p->map_file(filename);
    if (!mm.data) {
        // printf("Error: could not map file %s\n", filename); // todo; put this validation in platform map_file() code instead
        return mm;
    }
    
    // The file begins with an AnimatedMeshHeader.
    MeshHeader *header = (MeshHeader*) mm.data;
    
    // Set the vertex pointer and count.
    *vertexCount = header->vertexCount;
    *vertices = (unsigned char*) mm.data + header->vertexArrayOffset;
    
    // Set the index pointer and count.
    *indexCount = header->indexCount;
    *indices = (unsigned int*)((unsigned char*) mm.data + header->indexArrayOffset);
    
    // Set the bone frames pointer, bone count, and frame count.
    *boneCount = header->boneCount;
    *frameCount = header->frameCount;
    *boneFrames = (unsigned char*) mm.data + header->boneFramesArrayOffset;
    
    return mm;
}
/* MEMORY MAPPING TEXTURE */
typedef struct {
    int width;
    int height;
} ImageHeader;  
struct MappedMemory load_texture(struct Platform *p, const char *filename, int *out_width, int *out_height) {
    struct MappedMemory mm = p->map_file(filename);

    ImageHeader *header = (ImageHeader*)mm.data;
    *out_width  = header->width;
    *out_height = header->height;
    return mm;
}

#endif