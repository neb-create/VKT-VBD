# Realtime Vulkan Global Illumination Renderer
---

This is a renderer written with Vulkan that will support global illumination through diffuse irradiance probes and some TBD specular technique.

For this milestone, I spent a lot of time learning Vulkan and setting up the renderer.  I implemented textures and obj loading.  

![](ShowcaseMedia/chair.png)

Chair w/ Point Lights, Albedo Map, Roughness & Metallic Map

![](ShowcaseMedia/fullMetalRed.png)

![](ShowcaseMedia/metallicPointLight.png)

![](ShowcaseMedia/plasticPointLight.png)

![](ShowcaseMedia/roughBall.png)

Different albedos, roughnesses, and metallnesses on a blob.

After implementing point lights, I refactored the engine into classes like Texture, Material, and Shader, to make asset management easier.  Next, I will implement a naive triangle raytracer on the GPU, then I'll implement a PBRT based BVH for it, and then I'll implement irradiance probes that store diffuse convolution information with spherical harmonics.  I'll try to use that same spherical harmonics method to get a diffuse convolution for IBL.  After getting diffuse lighting working, I'll get specular working with specular convolution and some method like reflection probes or screenspace reflection.

---
## 3/5/2026 Update

- More refactoring for Vulkan Renderer: Mesh, RenderPass, RenderTarget, ComputeDispatcher, ComputeShader, Material classes...
- Naive raytracing of meshes test
- Render Pass & Target setup for rendering to arbitrary textures, setup for deferred rendering
- Diffuse Convolution using Spherical Harmonics (could be made faster using lookup tables for basis functions)

![](ShowcaseMedia/naiveChairReflection.gif)

Testing naive raytracing.

![](ShowcaseMedia/chairRenderInsidechairRenderCubemapRenderTargetPassTest.png)

Looks weird but it tests using RenderTarget & RenderPass objects to render to cubemap, useful for Specular Image-Based Lighting.  Specifically, render a skybox chair into the skybox's +Y cubemap face, then render the skybox chair using the modified skybox.

---

![](ShowcaseMedia/blobSkybox.gif)

Rendering the skybox into a sphere.

![](ShowcaseMedia/blobSHDiffuseConvolution.gif)

![](ShowcaseMedia/shChair.gif)

Rendering blob and chair using Spherical Harmonics Diffuse Convolution with white albedo.

<!-- ![](ShowcaseMedia/blobSHDiffuseConvolution.gif)

| Skybox | Diffuse Convolution using SH |
| :---: | :---: |
| ![](ShowcaseMedia/blobSkybox.gif) | ![](ShowcaseMedia/blobSHDiffuseConvolution.gif) |

Can bake  -->

---
## 3/18/2026 Update

- Baking many diffuse probes, no longer using float atomics so that the renderer has better compatibility
- More refactoring for Vulkan Renderer
- Render skybox cubemap

![](ShowcaseMedia/skyboxXZProbes.gif)

80x10x80 probe grid w/ skybox

![](ShowcaseMedia/noSkyboxXZProbes.gif)

80x10x80 probe grid w/o skybox

Need to do probe visibility checking & optimize probe bake compute shader, likely through BVH and parallelization.

---
## 3/26/2026 Update

![](ShowcaseMedia/cameraAndGameObjects.gif)

- Camera Controller
- UI Integration
- Uniform Ring Buffer for multiple game objects being stored optimally in same material

---
## 4/1/2026 Update
![](ShowcaseMedia/debugProbes.gif)

---
## Visibility/Weighting Plan

- Max dimensions of texture on device
- Octahedral depth and depth^2 map, big texture atlas, each individual probe takes up 18x18 (including gutters)
- During monte carlo irradiance estimation, for each sample, get depth and depth^2 and project into octahedral map.  In the 16x16 texture, each texel center corresponds to a sphere direction, do a weighted sum on some nearby texels (most accurate to do all, but could do like a 5x5 pixel radius) with max(0, dot(texelCenterDir, sampleRayDir)) as the weight.  Value += weight * sampleVal, totalWeight += weight, then at end Value /= totalWeight.  Do this for both depth and depth^2.  Maybe I should use a buffer to bake and then put it into a texture?
- After we bake the buffer/texture, we can turn it into a gutter texture, where each 16x16 grid is now 18x18 so that when a border texel looks to the outside, it loops back around for bilinear interpolation.
- For sampling probes, instead of trilinear interpolating the sample directly, we can trilinear interpolate a 'trilinear sample weight' so that if we do a weighted sum of the values with this weight, we get the same trilinear interpolated value.  w000 = (1-x) * (1-y) * (1-z)..., then we can tack on other weights easily.
- Other weights in addition to trilinear position weight (shown in below img), are backface weight and visibility weight.  Backface weight is a simple dot product visibility heuristic and visibility weight is the actual bilinear interpolated sample from the depth texture.
- How do we sample the depth textures though?  We have E(depth) and E(depth^2) so we can use that to get variance.  We then use the chebychev inequality so that visibility is smoother for depth samples that have higher variance.
![alt text](image.png)

## Scheduling
- Instead of each compute pass baking lots of rays for a probe, could make it so each compute pass bakes a few rays for each probe.  This avoids the scratch buffer and the atomics since we can just do smth more similar to monte calro pathtracing where we add into the texture each time.
- Could cycle through probes.  If I have like 64,000 probes, can bake like 256 probes per compute pass (each group does its own probe and we can have 256 thread groups), and we cycle through, so the probes that satisfy probeID % probeGroupCount == probeGroupIndex get baked.  The group size is 256 since we bake a group per pass, so group count should be ceil(64000/256).  We cycle through each group many times of course since 64 samples per probe isn't nearly enough.  We need to put a compute barrier before cycling back.
- To make the samples always valid, whenever we add on a pack of sampled sh coefficients, can have a sh[27] index reserved for SampleCount, so if SampleCount = 3*64 for 3 prev samples, assuming sh previously was valid, newSh = (oldSh * SampleCount + newSample * 64)/(SampleCount + 64) then sh[27] += 64.
```cpp
loop(bakeCount) {
    for(i in 0..groupCount) {
        // Each group gets 8x8 samples
        bakeGroup(i)
    }
    computeBarrier()
}
```

---
## 4/8/2026 Update

- Better probe scheduling method (a few samples from many probes each pass, fewer barriers/waits)
- Octahedral depth map for spheres, monte carlo samples splat depths onto nearby texels
- Large octahedral depth atlas where each 18x18 tile is dedicated to a probe.  16x16 center of each 18x18 tile stores the actual depth data, the border tile texels will never be accessed directly but are there for bilinear filtering to work correctly with octahedral mapping; going off of the 16x16 tile should loop according to the octahedron, not the 2D uv space.

![](ShowcaseMedia/bufferDepth.gif)

Using a buffer with depth values, no texture so no bilinear filtering.

![](ShowcaseMedia/interpolatedDepth.gif)

Using depth atlas for depth values, so we have bilinear filtering.  (Need to debug the borders but they're not due to incorrect octahedral looping).


---
## 4/15/2026 Update - Correct Occlusion

- Fixed a weird banding issue on the depth probes' octahedral mapping from last time.  Problem was just anisotropic filtering being on.
- Added depth^2 accumulation to probe depth estimation to allow computation of depth variance (helps since probe depth texels are so big 16x16 per probe only)
- For choosing probes to use when shading a point, implemented backface weighting to reject probes behind the normal (below diagram)

| ![Alt text](image-1.png) |
|:--:|
| Diagram from [GI Blog](https://handmade.network/p/75/monter/blog/p/7288-engine_work__global_illumination_with_irradiance_probes) |

- Also implemented depth based weighting to reject probes that are occluded (shadow mapping-like).  Used depth & depth^2 to construct a smooth falloff based on variance of a probe's depth samples.

![](ShowcaseMedia/badSituationButNoLightLeak.png)

This image is a light leak stress test.  For earlier renders, the probes were set up in a way to avoid possible light leaks, but here, probes are set up in the worst way possible.  There are probes underneath the ceiling and right outside the walls, and the room is thin.  There's still not much light leaking though.

### Problem

- Spherical Harmonics probably have a math issue somewhere still.  Things were messed up before and I did a jank fix which probably didn't actually fix it.  I think that's probably the reason for the light near the window's edge (I don't think it's interpolating from the 'wrong probes').

### Debugging

- Was annoying to debug the depth based weighting so I took pictures of the patterns that the bugs created

![](ShowcaseMedia/p7.png)

![](ShowcaseMedia/p5.png)

![](ShowcaseMedia/p8.png)

![](ShowcaseMedia/p9.png)

![](ShowcaseMedia/p2.png)

![](ShowcaseMedia/p4.png)

### Fixes
- Not having depth samples splat to neighbor particles seemed to help sometimes?
- Making sure the depth samples were truncated to the max distance to a probe so that depth samples of the skybox or far away stuff don't dominate and mess with nearby uvs/texels.
- Sampling bias: push the sampling point along the normal a bit to help the probes on the correct side of the wall and hurt the probes on the other side.
- Offset the average depth a tiny bit (after sampling from depth texture, increment it a bit).

