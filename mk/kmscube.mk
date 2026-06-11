## KMSCube build target
.PHONY: kmscube-build

kmscube-build:
	$(call kraft_build,kraft/Kraftfile.kmscube-vgpu-gl)
