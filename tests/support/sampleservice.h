#pragma once

#include "lumen/core/data/database.h"
#include "lumen/core/result.h"

namespace lumen::app {

// Test data: fills a database with the service shown in
// design/mockups/Main.html (public-domain hymns; scripture from the World
// English Bible). The app itself ships no sample content.
core::Status seedSampleService(core::data::Database &db);

} // namespace lumen::app
