# architecture

this documentation is written by Jerry. 

There are some ideas i want to implement/extend/explain in the project

## the analysis

### the dependency analysis:

#### Goal:

- first step is to identity the current problem in the Linux kernel's design. 
    - what is the current stage for runnig gpu application like llama.cpp/ggml in linux?
    - which path will the kernel choose for it?
    - what is the stack that the application will touch? 
        - for llama.cpp?
        - for ggml?
        - normal gpu application?
- draw the dependency diagram showing how to run the gpu application.
    - will it be complex?
    - what is the function call flow?
- For the core ABI/API of the linux kernel, unikraft, what are the differences
    - identify the scope and the complexity of porting gpu applicaiton natively.
    - how the current unikraft deal with tht systemcall mismach between linux abi and unikraft?
- after analyzing the interaction, the dependency of the gpu driver, virtio-gpu, virtio-pci, and the gpu application.
    - know how to design the interface in unikraft to minize the porting effort while maintaining the performance and the functionality of the gpu application.
    - based on the analysis, propose the new unikraft architecture for the gpu application.
- the advantage of unikraft is it make the developer to convience customize the unikernel to achieve the optimal performance and functionality for the gpu application.
    - why the unikraft can achieve the better performance?
    - I need to put the insigh to design the gpu driver for unikraft. 
    - is it possible to design a new driver for unikraft?
        - how to support vulkan?
        - how to support the shared memory?
        - how to support the interrupt?
        - how to support the DMA?
    - is it possible to leverage the existing driver?
        - how to leverage the existing driver?
        - how to leverage the virtio-gpu?
        - how to leverage the virtio-pci?

#### search

- what is the current main gpu driver?
- what is the dencpendency for the gpu driver to run?
        
#### implementation related

- if i want to support virtio-gpu vulkan in unikraft, what is the necessary components to run it?
- is it possible to leverage the existing driver?
    - only implement the minimal interface for virtio-gpu and leverage the current linux driver.
    - how to support the shared memory?
    - how to support the interrupt?
    - how to support the DMA?
- how to support the pci device?
- i want to port the full vulkan command serialization in unikraft to avoid the context switch between unikernel and the guest.
- How to design the interface in unikraft to make the developer to convience customize the unikernel to achieve the optimal performance and functionality for the gpu application.
- i want to split the virtio-gpu vulkan into various components. or make them can be selected to compile in the unikernel.
    - only compile the necessary components to run the target gpu application. 
    - the dependency when configuring the unikernel can be auto dected based on the target gpu application.
    

