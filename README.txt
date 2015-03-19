This library provides notable speed-up for seeking in large compressed NIfTI files.

It is compatible with the libznz interface which is part of NIfTI C library to handle multidimensional (mostly MRI) images. Only the replacement of the currently available libznz library is necessary.

To reach the expected speed-up the compressed NIfTI data has to be indexed with the included tool.

To compile the binaries, the sources available in this project have to be replaced with the ones in znzlib folder of project https://sourceforge.net/projects/niftilib/files/nifticlib/nifticlib_2_0_0/ and nifticlib has to be compiled as described.

After compiling binaries you can replace the actual version of libznz (installed by other software, e.g. FSL). For example in case of a dynamic library: "cd /usr/lib", with root privileges "ln -sf [mypathtothisproject]/libznz.so.2.zindex libznz.so.2".

For now only reading is supported by libznz, writing will be added later. Creating index files is currently provided by a separate tool: run "make zindex" in terminal in the project folder. Run "./zindex" for help.


