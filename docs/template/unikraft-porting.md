Unikraft Porting <AppName> (Template)
(Note: For a fully populated, concrete example of this template, please refer to the document: "Unikraft Porting - Sample - TigerBeetle")
Phase 1: Porting <AppName>

Stage 0: Operational Setup & Baselining:
Objective: Understanding exactly how <AppName> builds and runs in a standard Linux environment.
 
Repository Setup:
Fork the necessary repositories (e.g. unikraft/catalog).
Clone the fork to the local machine.
Create a working branch for the initial tests (e.g., feat/<AppName>-baselines).

Baseline 1 - Docker (pre-compiled):
Pull the official Docker image for <AppName>.
Run the container and expose the necessary ports.
Identify and document the minimal required configuration (CLI flags, config files, or environment variables) needed to start the server and route basic traffic.

Baseline 2 - Docker (from source):
Write a custom Dockerfile.source using the standard base image for the application's language.
Inside the above-mentioned Dockerfile, install build dependencies (e.g., git, make, gcc), then clone the official source code repository.
Compile the binary within the isolated container.
Build this container image locally.
Run the newly built container using the minimal configuration identified in Baseline 1 to verify the source compilation is fully functional.

Baseline 3 - Minimalistic Container (pre-compiled):
Write a Dockerfile.min.precompiled starting with the FROM scratch base image to simulate Unikraft's empty filesystem.
Copy the pre-compiled binary into this scratch image.
Note: Identify if the application requires external global trust anchors (like /etc/ssl/certs/ca-certificates.crt for HTTPS) or specific timezone data, and copy those into the scratch image as well.
Build the container image.
Run the container using the baseline configuration to verify <AppName> functions correctly without a full Linux operating system.



Baseline 4 - Minimalistic container (From Source):
Write a multi-stage Dockerfile.min.source
In the builder stage, use the language's compiler to generate a 100% statically linked binary (e.g., Go (CGO_ENABLED=0), C/C++ (-static), Rust (x86_64-unknown-linux-musl)).
Note: If omitted, the binary dynamically links to standard Linux C libraries (like glibc or musl) and immediately crashes when placed into an empty scratch or Unikraft unikernel environment where those libraries do not exist.
In the second stage, use the FROM scratch base image and copy the freshly compiled static binary (and the possible requirements mentioned in Baseline 3, Note Section) into it.
Build the container image.
Run the container using the baseline configuration to verify the static compilation works perfectly in an empty environment.
 
Stage 1: Binary Compatibility Port (catalog)
Objective: Run the pre-compiled Linux binary on top of Unikraft using the base:latest runtime (of app-elfloader) with KraftKit.
 
Environment Preparation:
Create a new branch inside the unikraft/catalog repository : feat/<AppName>-bincompat. 
Set up a new project directory with a basic Kraftfile targeting the base:latest runtime.

Filesystem Integration:
Configure the Kraftfile to mount a root filesystem (rootfs).
Place the pre-compiled <AppName> binary (from Stage 0) and its required configuration files into the rootfs.

Image Construction (Kraftfile Integration):
Update the Kraftfile to automate the filesystem creation by linking Dockerfile.min.precompiled (from Baseline 3) created in Stage 0.
Configure the Kraftfile to build the final image by passing this Dockerfile directly into the rootfs option.

Execution & Syscall Debugging:
Update the kconfig section of the Kraftfile to enable syscall_shim debugging (CONFIG_LIBSYSCALL_SHIM_DEBUG_SYSCALLS=y).
Enable verbose debug printing (CONFIG_UK_DEBUG=y) to expose deep internal uk_pr_debug messages from the network stack (lwip) and the virtual filesystem (vfs).
Attempt to boot the unikernel using kraft run. 
Monitor the log output.
Look at the last few strace prints before <AppName> crashes to identify which specific syscall is failing.
If a crash occurs that isn't clearly explained by the strace output, boot the unikernel with a GDB server attached (using kraft run -W or QEMU's -S -s flags). Attach GDB to inspect the exact backtrace and memory state.
Record all missing or failing syscalls. This list will dictate what patches or workarounds are needed during the Stage 2 Native Port.

Application Validation:
Once booted, send basic traffic to the exposed port to ensure the Unikraft network stack correctly routes to the process.
Note: If <AppName> relies on persistent storage (e.g., a database or file server), verify that it can successfully write to and read from its designated data files.

Operational Wrap-up (Bincompat):
Add a GitHub Actions workflow to automatically build and test the bincompat build.
Commit changes, push to the fork, and open a Pull Request against the upstream unikraft/catalog repository.

Stage 1b: Binary Compatibility Port (catalog-core)
Objective: Add <AppName> bincompat to the catalog-core repository.

Environment & Filesystem preparation:
Create a new branch inside the unikraft/catalog-core repository : feat/<AppName>-bincompat
Set up the project directory and create a rootfs folder to store the pre-compiled <AppName> binary and configuration files.

Build System & Script Integration:
Integrate the pre-compiled binary using the Makefile-based approach by creating the required Makefile, Makefile.uk, Config.uk.
Configure the build to target the appropriate elfloader profile based on <AppName>'s requirements. Use elfloader-base if the application does not require networking, or elfloader-net if the network stack is needed.
Note:  If the application requires specific kernel features not present in the default profiles, a custom profile could be created and configured to better match the requirements (e.g., elfloader-custom).
Create a custom run.sh script to manually orchestrate the unikernel boot process via QEMU.
Detail the QEMU hardware arguments inside the script.
Reference Material: For working examples of boot scripts configuring host networking or persistent storage, refer to existing application ports (like Nginx for networking, or Redis/SQLite for storage) in the catalog-core repository. Adapt their QEMU arguments (-netdev, -drive, -fsdev, etc.) to match <AppName>'s specific requirements.

Execution & Wrap-up:
Run and test the bincompat application locally.
Open a Pull Request against the upstream catalog-core repository.
Stage 2: Native Port (catalog-core)
Objective: Compile the <AppName> source code directly into a highly optimized unikernel image.

Prerequisite step:
Create a new, dedicated repository named lib-<AppName> to host the Unikraft library configuration.
Create and configure the three mandatory Unikraft build files inside this repository: Config.uk, Makefile, and Makefile.uk

Build System Integration:
Create a new branch inside the unikraft/catalog-core repository: feat/<AppName>-native
Adapt Unikraft's Makefile system to fetch the <AppName> source code.
Create a patches/ directory within the root of the lib-<AppName> repository.
Based on the missing syscalls and crashes documented in Stage 1, modify the fetched <AppName> source code to bypass unsupported Linux features.
Generate standard unified diffs for the modifications (e.g., using diff -urN patches/0001-bypass-unsupported-syscalls.patch).
Configure Makefile.uk to point to this directory so the Unikraft build system automatically applies the patches during the fetch/extract phase before compilation.
Resolve compile-time linking errors, properly map standard library dependencies and compile the native unikernel using standard make commands.
Open a Pull Request against the upstream catalog-core repository.

Stage 2b: Native Port (catalog)
Objective: Integrate the natively compiled <AppName> unikernel into the catalog repository using KraftKit.

Dependency Mapping & Kraftfile Creation:
Create a new branch inside the unikraft/catalog repository: feat/<AppName>-native.
Create a dedicated application directory (e.g., apps/<AppName>-native)
Write a native Kraftfile that specifically points to the lib-<AppName> repository (created in Stage 2) as a library dependency.
Explicitly map all necessary Unikraft core libraries (e.g., lib-lwip, memory allocators and filesystem drivers) within the Kraftfile's libraries block.


Execution & Validation:
Build the native unikernel using kraft build
Resolve any compile-time linking errors or missing dependencies.
Run the native unikernel using kraft run and repeat the application-specific validation tests (e.g., network routing, file I/O).
Open a Pull Request against the upstream catalog repository. 

Operational Wrap-up (Native):
Add/Update GitHub Actions workflows for native build and test integration. Specifically, review and update the YAML workflow files inside the .github/workflows/ directory for both the catalog and catalog-core repositories.
Create a Pull Request, request reviews, implement feedback and merge.
 

