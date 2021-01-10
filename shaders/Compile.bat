del /f pathtrace.rgen.spv
del /f raytrace.rgen.spv
del /f pathtrace.rmiss.spv
del /f raytrace.rmiss.spv
del /f raytraceShadow.rmiss.spv
del /f pathtrace.rchit.spv
del /f raytrace.rchit.spv
del /f raytraceIS.rchit.spv
del /f pathtraceIS.rchit.spv
del /f raytrace.rint.spv
del /f post.frag.spv


glslangValidator pathtrace.rgen -V -o pathtrace.rgen.spv -g
glslangValidator raytrace.rgen -V -o raytrace.rgen.spv -g
glslangValidator raytrace.rmiss -V -o raytrace.rmiss.spv
glslangValidator pathtrace.rmiss -V -o pathtrace.rmiss.spv
glslangValidator raytraceShadow.rmiss -V -o raytraceShadow.rmiss.spv
glslangValidator pathtrace.rchit -V -o pathtrace.rchit.spv
glslangValidator raytrace.rchit -V -o raytrace.rchit.spv
glslangValidator raytraceIS.rchit -V -o raytraceIS.rchit.spv
glslangValidator pathtraceIS.rchit -V -o pathtraceIS.rchit.spv
glslangValidator raytrace.rint -V -o raytrace.rint.spv
glslangValidator post.frag -V -o post.frag.spv

PAUSE




