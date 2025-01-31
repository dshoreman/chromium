// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MarkingVisitor_h
#define MarkingVisitor_h

#include "platform/heap/Heap.h"
#include "platform/heap/HeapPage.h"
#include "platform/heap/Visitor.h"

namespace blink {

// Visitor used to mark Oilpan objects.
class PLATFORM_EXPORT MarkingVisitor final : public Visitor {
 public:
  enum MarkingMode {
    // This is a default visitor. This is used for GCType=GCWithSweep
    // and GCType=GCWithoutSweep.
    kGlobalMarking,
    // This visitor just marks objects and ignores weak processing.
    // This is used for GCType=TakeSnapshot.
    kSnapshotMarking,
    // This visitor is used to trace objects during weak processing.
    // This visitor is allowed to trace only already marked objects.
    kWeakProcessing,
    // Perform global marking along with preparing for additional sweep
    // compaction of heap arenas afterwards. Compared to the GlobalMarking
    // visitor, this visitor will also register references to objects
    // that might be moved during arena compaction -- the compaction
    // pass will then fix up those references when the object move goes
    // ahead.
    kGlobalMarkingWithCompaction,
  };

  static std::unique_ptr<MarkingVisitor> Create(ThreadState*, MarkingMode);

  MarkingVisitor(ThreadState*, MarkingMode);
  virtual ~MarkingVisitor();

  inline MarkingMode GetMarkingMode() const { return marking_mode_; }

  // Marking implementation.

  // This method marks an object and adds it to the set of objects that should
  // have their trace method called. Since not all objects have vtables we have
  // to have the callback as an explicit argument, but we can use the templated
  // one-argument mark method above to automatically provide the callback
  // function.
  inline void Mark(const void* object_pointer, TraceCallback);

  // Used to mark objects during conservative scanning.
  inline void MarkHeader(HeapObjectHeader*, TraceCallback);
  inline void MarkHeaderNoTracing(HeapObjectHeader*);

  // Marks the header of an object. Is used for eagerly tracing of objects.
  inline bool EnsureMarked(const void* pointer);

  // Used for eagerly marking objects and for delayed marking of backing stores
  // when the actual payload is processed differently, e.g., by weak handling.
  inline void MarkNoTracing(const void* pointer) {
    Mark(pointer, reinterpret_cast<TraceCallback>(0));
  }

  // Implementation of the visitor interface. See above for descriptions.

  void Visit(void* object, TraceDescriptor desc) final {
    // Default mark method of the trait just calls the two-argument mark
    // method on the visitor. The second argument is the static trace method
    // of the trait, which by default calls the instance method
    // trace(Visitor*) on the object.
    //
    // If the trait allows it, invoke the trace callback right here on the
    // not-yet-marked object.
    if (desc.can_trace_eagerly) {
      // Protect against too deep trace call chains, and the
      // unbounded system stack usage they can bring about.
      //
      // Assert against deep stacks so as to flush them out,
      // but test and appropriately handle them should they occur
      // in release builds.
      //
      // If you hit this assert, it means that you're creating an object
      // graph that causes too many recursions, which might cause a stack
      // overflow. To break the recursions, you need to add
      // WILL_NOT_BE_EAGERLY_TRACED_CLASS() to classes that hold pointers
      // that lead to many recursions.
      DCHECK(Heap().GetStackFrameDepth().IsAcceptableStackUse());
      if (LIKELY(Heap().GetStackFrameDepth().IsSafeToRecurse())) {
        if (EnsureMarked(desc.base_object_payload)) {
          desc.callback(this, desc.base_object_payload);
        }
        return;
      }
    }
    Mark(desc.base_object_payload, desc.callback);
  }

  void VisitBackingStoreStrongly(void* object,
                                 void** object_slot,
                                 TraceDescriptor desc) final {
    RegisterBackingStoreReference(object_slot);
    Visit(object, desc);
  }

  // Used to delay the marking of objects until the usual marking including
  // ephemeron iteration is done. This is used to delay the marking of
  // collection backing stores until we know if they are reachable from
  // locations other than the collection front object. If collection backings
  // are reachable from other locations we strongify them to avoid issues with
  // iterators and weak processing.
  void VisitBackingStoreWeakly(void* object,
                               void** object_slot,
                               TraceDescriptor desc) final {
    DCHECK(GetMarkingMode() != kWeakProcessing);
    RegisterBackingStoreReference(object_slot);
    Heap().PushPostMarkingCallback(object, &MarkNoTracingCallback);
  }

  void RegisterBackingStoreCallback(void* backing_store,
                                    MovingObjectCallback,
                                    void* callback_data) final;
  bool RegisterWeakTable(const void* closure,
                         EphemeronCallback iteration_callback,
                         EphemeronCallback iteration_done_callback) final;
#if DCHECK_IS_ON()
  bool WeakTableRegistered(const void* closure) final;
#endif
  void RegisterWeakCallback(void* closure, WeakCallback) final;

 private:
  static void MarkNoTracingCallback(Visitor*, void*);

  void RegisterBackingStoreReference(void* slot);

  const MarkingMode marking_mode_;
};

inline void MarkingVisitor::MarkHeader(HeapObjectHeader* header,
                                       TraceCallback callback) {
  DCHECK(header);
  if (header->IsMarked())
    return;

  DCHECK(ThreadState::Current()->IsInGC() ||
         ThreadState::Current()->IsIncrementalMarking());
  DCHECK(GetMarkingMode() != kWeakProcessing);

  const void* object_pointer = header->Payload();
  // A GC should only mark the objects that belong in its heap.
  DCHECK(&PageFromObject(object_pointer)->Arena()->GetThreadState()->Heap() ==
         &Heap());

  header->Mark();

  if (callback)
    Heap().PushTraceCallback(const_cast<void*>(object_pointer), callback);
}

inline void MarkingVisitor::Mark(const void* object_pointer,
                                 TraceCallback callback) {
  if (!object_pointer)
    return;
  HeapObjectHeader* header = HeapObjectHeader::FromPayload(object_pointer);
  MarkHeader(header, callback);
}

inline void MarkingVisitor::MarkHeaderNoTracing(HeapObjectHeader* header) {
  MarkHeader(header, reinterpret_cast<TraceCallback>(0));
}

inline bool MarkingVisitor::EnsureMarked(const void* object_pointer) {
  if (!object_pointer)
    return false;

  HeapObjectHeader* header = HeapObjectHeader::FromPayload(object_pointer);
  if (header->IsMarked())
    return false;
#if DCHECK_IS_ON()
  MarkNoTracing(object_pointer);
#else
  // Inline what the above markNoTracing() call expands to,
  // so as to make sure that we do get all the benefits (asserts excepted.)
  header->Mark();
#endif
  return true;
}

}  // namespace blink

#endif  // MarkingVisitor_h
