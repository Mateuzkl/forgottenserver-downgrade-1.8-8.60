// Copyright 2023 The Forgotten Server Authors. All rights reserved.
// Use of this source code is governed by the GPL-2.0 License that can be found in the LICENSE file.

#ifndef FS_OBSERVER_PTR_H
#define FS_OBSERVER_PTR_H

// Non-owning pointer alias. ObserverPtr documents that the pointee lifetime is
// managed elsewhere; do not store it across async/scheduler boundaries without
// an external lifetime guard such as an id lookup, weak_ptr, or owning ref.
template <typename T>
using ObserverPtr = T*;

#endif
