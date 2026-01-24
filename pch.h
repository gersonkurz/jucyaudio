#pragma once

// Precompiled header for jucyaudio project code only (no JUCE/third-party).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Database/Includes/AlbumInfo.h>
#include <Database/Includes/Constants.h>
#include <Database/Includes/DataColumn.h>
#include <Database/Includes/ExportFolderInfo.h>
#include <Database/Includes/FolderInfo.h>
#include <Database/Includes/IRefCounted.h>
#include <Database/Includes/LibraryRootInfo.h>
#include <Database/Includes/MixMarker.h>
#include <Database/Includes/TrackInfo.h>
#include <Database/Includes/TrackMarker.h>
#include <Database/Includes/TrackQueryArgs.h>
#include <Database/Includes/WorkingSetInfo.h>
#include <Utils/AtomicSharedPtr.h>
