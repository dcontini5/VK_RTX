del /f pathtrace.rgen.spv
del /f raytrace.rmiss.spv
del /f raytraceShadow.rmiss.spv
del /f pathtrace.rchit.spv
del /f raytrace2.rchit.spv
del /f raytrace.rint.spv


glslangValidator -V pathtrace.rgen  -o pathtrace.rgen.spv -g
glslangValidator raytrace.rmiss -V -o raytrace.rmiss.spv
glslangValidator raytraceShadow.rmiss -V -o raytraceShadow.rmiss.spv
glslangValidator pathtrace.rchit -V -o pathtrace.rchit.spv
glslangValidator raytrace2.rchit -V -o raytrace2.rchit.spv
glslangValidator raytrace.rint -V -o raytrace.rint.spv

PAUSE




