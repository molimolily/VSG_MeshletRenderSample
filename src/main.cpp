#include <iostream>
#include <vsg/all.h>
#include "ModelLoader.h"


int main(int argc, char** argv)
{
    try
    {
        auto windowTraits = vsg::WindowTraits::create();
        windowTraits->windowTitle = "MeshletRenderSample";
        windowTraits->vulkanVersion = VK_API_VERSION_1_2;
        windowTraits->deviceExtensionNames = { VK_EXT_MESH_SHADER_EXTENSION_NAME, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME };

        auto features = windowTraits->deviceFeatures = vsg::DeviceFeatures::create();
        
        auto& vulkan12Features = features->get<VkPhysicalDeviceVulkan12Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES>();
        vulkan12Features.shaderInt8 = 1;
        vulkan12Features.storageBuffer8BitAccess = 1;

        auto& meshFeatures = features->get<VkPhysicalDeviceMeshShaderFeaturesEXT, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT>();
        meshFeatures.meshShader = 1;
        meshFeatures.taskShader = 1;

        auto window = vsg::Window::create(windowTraits);
        if (!window)
        {
            std::cout << "Could not create window." << std::endl;
            return 1;
        }

        auto vulkan12_features = window->getOrCreatePhysicalDevice()->getFeatures<VkPhysicalDeviceVulkan12Features, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES>();
        if (!vulkan12_features.shaderInt8)
		{
			std::cout << "Int8 storage not supported." << std::endl;
			return 1;
		}

        if (!vulkan12_features.storageBuffer8BitAccess)
        {
            std::cout << "8-bit storage buffer access not supported." << std::endl;
            return 1;
        }

        auto mesh_features = window->getOrCreatePhysicalDevice()->getFeatures<VkPhysicalDeviceMeshShaderFeaturesEXT, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT>();

        if (!mesh_features.meshShader || !mesh_features.taskShader)
        {
            std::cout << "Mesh shaders not supported." << std::endl;
            return 1;
        }

        auto meshShaderPath = "../shader/meshlet.mesh";
        auto fragShaderPath = "../shader/meshlet.frag";

        auto meshShader = vsg::read_cast<vsg::ShaderStage>(meshShaderPath);
        auto fragmentShader = vsg::read_cast<vsg::ShaderStage>(fragShaderPath);

        if (!meshShader || !fragmentShader)
        {
            std::cout << "Could not create shaders." << std::endl;
            return 1;
        }

        auto shaderStages = vsg::ShaderStages{ meshShader, fragmentShader };

        auto model = ModelLoader::loadModel("../model/bunny.obj");
        if(model.empty())
		{
			std::cout << "Failed to load model." << std::endl;
			return 1;
		}
        auto mesh = model[0];
        std::cout << "indices size: " << mesh->indices->size() << ", vertices size = " << mesh->vertices->size() << ", meshlets size = " << mesh->meshlets->size() << std::endl;

        uint32_t vertexBufferBinding = 0;
        uint32_t meshletBufferBinding = 1;
        uint32_t meshletVertexBufferBinding = 2;
        uint32_t meshletPrimitiveBufferBinding = 3;
        uint32_t meshPropertyBufferBinding = 4;
        auto vertexBuffer = vsg::DescriptorBuffer::create(mesh->vertices, vertexBufferBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        auto meshletBuffer = vsg::DescriptorBuffer::create(mesh->meshlets, meshletBufferBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        auto meshletVertexBuffer = vsg::DescriptorBuffer::create(mesh->meshletVertices, meshletVertexBufferBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        auto meshletPrimitiveBuffer = vsg::DescriptorBuffer::create(mesh->meshletPrimitives, meshletPrimitiveBufferBinding, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        auto meshPropertyArray = vsg::uintArray::create(3);
        meshPropertyArray->at(0) = static_cast<unsigned int>(mesh->meshletCount);
        auto meshPropertyBuffer = vsg::DescriptorBuffer::create(meshPropertyArray, meshPropertyBufferBinding, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

        vsg::Descriptors descriptors{
			vertexBuffer,
            meshletBuffer,
            meshletVertexBuffer,
            meshletPrimitiveBuffer,
			meshPropertyBuffer
		};

        vsg::DescriptorSetLayoutBindings descriptorBindings{
            {vertexBufferBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_MESH_BIT_EXT, nullptr},
            {meshletBufferBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_MESH_BIT_EXT, nullptr},
            {meshletVertexBufferBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_MESH_BIT_EXT, nullptr},
            {meshletPrimitiveBufferBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_MESH_BIT_EXT, nullptr},
            {meshPropertyBufferBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_MESH_BIT_EXT, nullptr}
        };
        auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);
        vsg::GraphicsPipelineStates pipelineStates{
            vsg::InputAssemblyState::create(),
            vsg::RasterizationState::create(),
            vsg::MultisampleState::create(),
            vsg::ColorBlendState::create(),
            vsg::DepthStencilState::create() };

        vsg::PushConstantRanges pushConstantRanges{
            {VK_SHADER_STAGE_MESH_BIT_EXT, 0, 128} // projection, view, and model matrices, actual push constant calls automatically provided by the VSG's RecordTraversal
        };

        auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{ descriptorSetLayout }, pushConstantRanges);
        auto graphicsPipeline = vsg::GraphicsPipeline::create(pipelineLayout, shaderStages, pipelineStates);
        auto bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);

        auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, descriptors);
        auto bindDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, descriptorSet);

        auto scenegraph = vsg::StateGroup::create();
        scenegraph->add(bindGraphicsPipeline);
        scenegraph->add(bindDescriptorSet);

        scenegraph->addChild(vsg::DrawMeshTasks::create(mesh->meshletCount, 1, 1));

        auto perspective = vsg::Perspective::create(60.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), 0.001, 10.0);
        auto lookAt = vsg::LookAt::create(vsg::dvec3(0.0, 2.0, 3.0), vsg::dvec3(0.0, 0.5, 0.0), vsg::dvec3(0.0, 0.0, -1.0));
        auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(window->extent2D()));

        auto viewer = vsg::Viewer::create();
        viewer->addWindow(window);
        viewer->addEventHandlers({ vsg::CloseHandler::create(viewer) });
        viewer->addEventHandler(vsg::Trackball::create(camera));

        auto commandGraph = vsg::createCommandGraphForView(window, camera, scenegraph);
        viewer->assignRecordAndSubmitTaskAndPresentation({ commandGraph });

        viewer->compile();

        while (viewer->advanceToNextFrame())
        {
            viewer->handleEvents();

            viewer->update();

            viewer->recordAndSubmit();

            viewer->present();
        }
    }
    catch (const vsg::Exception& exception)
    {
        std::cout << exception.message << " VkResult = " << exception.result << std::endl;
        return 0;
    }

    return 0;
}