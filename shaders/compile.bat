slangc pbr-test.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o pbr-test.spv
slangc test-compute.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry main -o test-compute.spv
slangc spherical-harmonics-sky.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry main -o spherical-harmonics-sky.spv
slangc spherical-harmonics-env.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry main -o spherical-harmonics-env.spv
slangc spherical-harmonics-env-prog.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry main -o spherical-harmonics-env-prog.spv
slangc skybox.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o skybox.spv
slangc reflect.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o reflect.spv