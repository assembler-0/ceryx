#include <mm/pmm.hpp>
#include <mm/kheap.hpp>
#include <FoundationKitCxxStl/Base/Vector.hpp>
#include <FoundationKitCxxStl/Base/Optional.hpp>
#include <FoundationKitMemory/GlobalAllocator.hpp>
#include <FoundationKitMemory/ObjectAllocator.hpp>
#include <lib/linearfb.hpp>

using namespace ceryx::mm;
using namespace FoundationKitCxxStl;
using namespace FoundationKitMemory;

struct MyObject {
    int id;
    const char* name;

    MyObject() : id(0), name("Unknown") {}
    MyObject(int i, const char* n) : id(i), name(n) {}
};

extern "C" void start_kernel() {
    linearfb_console_init();
    FK_LOG_INFO("ceryx (R) - FoundationKit demonstration kernel.");
    FK_LOG_INFO("ceryx: entering bootstrap");
    FK_LOG_INFO("FoundationKit: Subsystem discovery starting...");

    // 1. Initialize Physical Memory Manager (Buddy Allocator)
    auto& pmm = Pmm::Get();
    pmm.Initialize();

    if (pmm.IsInitialized()) {
        FK_LOG_INFO("PMM: Buddy Allocator initialized with usable memory.");

        // 2. Initialize Kernel Heap (Slab Allocator with PMM fallback)
        auto& kheap = KHeap::Get();
        kheap.Initialize(pmm);

        if (kheap.IsInitialized()) {
            FK_LOG_INFO("KHeap: Slab-based kernel heap ready.");

            // 3. Set the global allocator for STL-like containers
            InitializeGlobalAllocator(kheap);
            FK_LOG_INFO("GlobalAllocator: Successfully bridged to KHeap.");

            // 4. Test High-Level Containers (Dynamic Memory)
            FK_LOG_INFO("Demo: Testing STL-like Vector...");
            Vector<int> vec;
            for (int i = 1; i <= 5; ++i) {
                vec.PushBack(i * 10);
            }

            FK_LOG_INFO("Vector: Size is {}, Capacity is {}", vec.Size(), vec.Capacity());

            // 5. Test Object Allocator (Managed objects with checksums)
            FK_LOG_INFO("Demo: Testing Managed Object Allocator...");
            ObjectAllocator obj_alloc(kheap);
            
            auto obj_res = obj_alloc.Allocate<MemoryObjectType::UserBase, MyObject>(42, "FoundationKit managed object");
            if (obj_res) {
                MyObject* obj = obj_res.Value();
                FK_LOG_INFO("Object: Created '{}' with ID {}", obj->name, obj->id);

                // Introspect live objects!
                FK_LOG_INFO("Introspection: Walking live objects of type UserBase...");
                obj_alloc.ForEachObject<MemoryObjectType::UserBase, MyObject>([](MyObject* o) {
                    FK_LOG_INFO(" -> Found live object: {} (ID: {})", o->name, o->id);
                });

                obj_alloc.Deallocate(obj);
                FK_LOG_INFO("Object: Successfully deallocated and unlinked.");
            }

            FK_LOG_INFO("Demo: Testing raw new/delete...");
            int *arr = new int[10];
            for (int i = 0; i < 10; i++) arr[i] = i;
            delete[] arr;
            FK_LOG_INFO("Raw new/delete test complete.");

            FK_LOG_INFO("FoundationKit Memory Demo Complete!");
        }
    } else {
        FK_LOG_ERR("PMM: Failed to find usable memory regions.");
    }

    FK_LOG_INFO("Halt.\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
