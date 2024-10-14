#pragma once
#include <iostream>
#include <vector>
#include <chrono>
#include <vsg/core/Array.h>
#include <vsg/core/Inherit.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <meshoptimizer.h>

struct InterleavedVertex
{
    vsg::vec3 position;
    float padding1 = 1.0f;
    vsg::vec3 normal;
    float padding2 = 0.0f;
    vsg::vec2 texCoord;
    float padding3[2] = { 0.0f, 0.0f };
};

using InterleavedVertexArray = vsg::Array<InterleavedVertex>;
using MeshletArray = vsg::Array<meshopt_Meshlet>;

class Meshlet : public vsg::Inherit<vsg::Object, Meshlet>
{
public:
    static const size_t maxVerticesPerMeshlet = 64;
    static const size_t maxPrimitivesPerMeshlet = 124;
    float coneWeight = 0.5f;
    vsg::ref_ptr<InterleavedVertexArray> vertices;
    vsg::ref_ptr<vsg::uintArray> indices;
    vsg::ref_ptr<MeshletArray> meshlets;
    vsg::ref_ptr<vsg::uintArray> meshletVertices;
    vsg::ref_ptr<vsg::ubyteArray> meshletPrimitives;
    size_t meshletCount;
};

class ModelLoader
{
public:
    static std::vector<vsg::ref_ptr<Meshlet>> loadModel(const vsg::Path& path)
    {
        std::vector<vsg::ref_ptr<Meshlet>> meshlets;
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path.string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GenNormals | aiProcess_OptimizeGraph | aiProcess_OptimizeMeshes);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            std::cerr << "Error: " << importer.GetErrorString() << std::endl;
            return meshlets;
        }

        processNode(scene->mRootNode, scene, meshlets);

        return meshlets;
    };

private:
    static void processNode(aiNode* node, const aiScene* scene, std::vector<vsg::ref_ptr<Meshlet>>& meshlets)
    {
        for (unsigned int i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            processMesh(mesh, scene, meshlets);
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++)
        {
            processNode(node->mChildren[i], scene, meshlets);
        }
    }

    static void processMesh(aiMesh* mesh, const aiScene* scene, std::vector<vsg::ref_ptr<Meshlet>>& meshlets)
    {
        auto meshlet = Meshlet::create();
        meshlet->vertices = InterleavedVertexArray::create(mesh->mNumVertices);
        meshlet->indices = vsg::uintArray::create(mesh->mNumFaces * 3);

        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            InterleavedVertex vertex = {};
            vsg::vec3 position;
            position.x = mesh->mVertices[i].x;
            position.y = mesh->mVertices[i].y;
            position.z = mesh->mVertices[i].z;
            vertex.position = position;

            vsg::vec3 normal;
            normal.x = mesh->mNormals[i].x;
            normal.y = mesh->mNormals[i].y;
            normal.z = mesh->mNormals[i].z;
            vertex.normal = normal;

            if (mesh->mTextureCoords[0])
            {
                vsg::vec2 texCoord;
                texCoord.x = mesh->mTextureCoords[0][i].x;
                texCoord.y = mesh->mTextureCoords[0][i].y;
                vertex.texCoord = texCoord;
            }
            else
            {
                vertex.texCoord = vsg::vec2(0.0f, 0.0f);
            }

            meshlet->vertices->at(i) = vertex;
        }

        unsigned int index = 0;
        for (unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++)
            {
                meshlet->indices->at(index++) = face.mIndices[j];
            }
        }

        optimizeMesh(meshlet);
        if (!generateMeshlets(meshlet))
        {
            std::cerr << "Failed to generate meshlets." << std::endl;
            return;
        }
        meshlets.push_back(meshlet);
    }

    static void optimizeMesh(vsg::ref_ptr<Meshlet> meshlet)
	{
        auto remapTable = vsg::uintArray::create(meshlet->indices->size());
        auto vertexCount = meshopt_generateVertexRemap(remapTable->data(), meshlet->indices->data(), meshlet->indices->size(), meshlet->vertices->data(), meshlet->vertices->size(), sizeof(InterleavedVertex));
        auto remappedVertices = InterleavedVertexArray::create(vertexCount);
        auto remappedIndices = vsg::uintArray::create(meshlet->indices->size());

        // remap the vertex and index buffers
        meshopt_remapVertexBuffer(remappedVertices->data(), meshlet->vertices->data(), meshlet->vertices->size(), sizeof(InterleavedVertex), remapTable->data());
        meshopt_remapIndexBuffer(remappedIndices->data(), meshlet->indices->data(), meshlet->indices->size(), remapTable->data());

        // replace the original buffers with the optimized ones
        meshlet->vertices = remappedVertices;
        meshlet->indices = remappedIndices;

        // optimize the index buffer for vertex cache and vertex fetch
        meshopt_optimizeVertexCache(meshlet->indices->data(), meshlet->indices->data(), meshlet->indices->size(), vertexCount);
        meshopt_optimizeVertexFetch(meshlet->vertices->data(), meshlet->indices->data(), meshlet->indices->size(), meshlet->vertices->data(), meshlet->vertices->size(), sizeof(InterleavedVertex));
	}

    static bool generateMeshlets(vsg::ref_ptr<Meshlet> meshlet)
	{
        auto start = std::chrono::high_resolution_clock::now();
		auto maxMeshlets = meshopt_buildMeshletsBound(meshlet->indices->size(), Meshlet::maxVerticesPerMeshlet, Meshlet::maxPrimitivesPerMeshlet);

        meshopt_Meshlet* tmpMeshlets = static_cast<meshopt_Meshlet*>(std::malloc(maxMeshlets * sizeof(meshopt_Meshlet)));
        uint32_t* tmpMeshletVertices = static_cast<uint32_t*>(std::malloc(maxMeshlets * Meshlet::maxVerticesPerMeshlet * sizeof(uint32_t)));
        uint8_t* tmpMeshletPrimitives = static_cast<uint8_t*>(std::malloc(maxMeshlets * Meshlet::maxPrimitivesPerMeshlet * sizeof(uint8_t)));

        if (!tmpMeshlets || !tmpMeshletVertices || !tmpMeshletPrimitives)
        {
            free(tmpMeshlets);
            free(tmpMeshletVertices);
            free(tmpMeshletPrimitives);
            std::cerr << "Failed to allocate memory for meshlets." << std::endl;
            return false;
        }

        auto meshletCount = meshopt_buildMeshlets(
			tmpMeshlets,
			tmpMeshletVertices,
			tmpMeshletPrimitives,
			meshlet->indices->data(),
			meshlet->indices->size(),
			&meshlet->vertices->data()->position.x,
			meshlet->vertices->size(),
			sizeof(InterleavedVertex),
			Meshlet::maxVerticesPerMeshlet,
			Meshlet::maxPrimitivesPerMeshlet,
			meshlet->coneWeight);

        auto last = tmpMeshlets[meshletCount - 1];
        auto meshletVerticesCount = last.vertex_offset + last.vertex_count;
        auto meshletPrimitivesCount = last.triangle_offset + ((last.triangle_count * 3 + 3) & ~3);
        auto reallocMeshlets = (meshopt_Meshlet*)realloc(tmpMeshlets, meshletCount * sizeof(meshopt_Meshlet));
        auto reallocMeshletVertices = (uint32_t*)realloc(tmpMeshletVertices, meshletVerticesCount * sizeof(uint32_t));
        auto reallocMeshletPrimitives = (uint8_t*)realloc(tmpMeshletPrimitives, meshletPrimitivesCount * sizeof(uint8_t));
        if(!reallocMeshlets || !reallocMeshletVertices || !reallocMeshletPrimitives)
		{
            free(tmpMeshlets);
            free(tmpMeshletVertices);
            free(tmpMeshletPrimitives);
            std::cerr << "Failed to reallocate memory for meshlets." << std::endl;
            return false;
		}

		meshlet->meshlets = MeshletArray::create(meshletCount, reallocMeshlets);
        meshlet->meshlets->properties.allocatorType = vsg::AllocatorType::ALLOCATOR_TYPE_MALLOC_FREE;
		meshlet->meshletVertices = vsg::uintArray::create(meshletVerticesCount, reallocMeshletVertices);
        meshlet->meshletVertices->properties.allocatorType = vsg::AllocatorType::ALLOCATOR_TYPE_MALLOC_FREE;
		meshlet->meshletPrimitives = vsg::ubyteArray::create(meshletPrimitivesCount, reallocMeshletPrimitives);
        meshlet->meshletPrimitives->properties.allocatorType = vsg::AllocatorType::ALLOCATOR_TYPE_MALLOC_FREE;
		meshlet->meshletCount = meshletCount;

        return true;
	}
};
