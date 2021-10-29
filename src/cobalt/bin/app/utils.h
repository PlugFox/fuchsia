// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_UTILS_H_
#define SRC_COBALT_BIN_APP_UTILS_H_

#include <fuchsia/cobalt/cpp/fidl.h>
#include <fuchsia/metrics/cpp/fidl.h>
#include <stdlib.h>

#include <string>

#include "third_party/cobalt/src/logger/status.h"

namespace cobalt {

fuchsia::cobalt::Status ToCobaltStatus(logger::Status s);

fuchsia::cobalt::Status ToCobaltStatus(util::Status s);

fuchsia::metrics::Status ToMetricsStatus(logger::Status s);

fuchsia::metrics::Status ToMetricsStatus(util::Status s);

// Reads the PEM file at the specified path and returns the contents as
// a string. CHECK fails if the file cannot be read.
std::string ReadPublicKeyPem(const std::string& pem_file_path);

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_UTILS_H_
