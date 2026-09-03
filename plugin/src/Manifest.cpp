#include "Manifest.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "AudioXLPlugin.hpp"
#include "JsonLite.hpp"
#include "SoundRegistry.hpp"

namespace AudioXLNS {

namespace {

namespace fs = std::filesystem;

bool RowToSpec(const JsonValue& aRow, const fs::path& aDir, SoundSpec& aOut, std::string& aWhy) {
  if (!aRow.IsObject()) {
    aWhy = "row is not an object";
    return false;
  }
  aOut.name = aRow.GetString("name");
  aOut.type = aRow.GetString("type", "mod_sfx_2d");
  if (aOut.name.empty()) {
    aWhy = "missing name";
    return false;
  }
  aOut.speaker = aRow.GetString("speaker");
  if (const JsonValue* sub = aRow.Get("subtitle")) {
    if (sub->IsString()) {
      aOut.subtitles[""] = sub->str;
    } else if (sub->IsObject()) {
      for (const auto& kv : sub->obj) {
        if (kv.second.IsString()) {
          std::string loc = kv.first;
          for (auto& c : loc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          aOut.subtitles[loc] = kv.second.str;
        }
      }
    }
  }
  const std::string file = aRow.GetString("file");
  if (file.empty()) {
    if (aOut.type == "mod_skip") {
      aOut.path.clear();   
    } else {
      aWhy = "missing file";
      return false;
    }
  } else {
    fs::path p = aDir / file;
    if (!fs::exists(p)) p = aDir / "customSounds" / file;
    if (!fs::exists(p)) {
      aWhy = "file not found: " + file;
      return false;
    }
    aOut.path = p.lexically_normal().string();
  }
  aOut.gain = static_cast<float>(aRow.GetNumber("gain", 1.0));
  aOut.pitch = static_cast<float>(aRow.GetNumber("pitch", 0.0));
  aOut.distance = static_cast<float>(aRow.GetNumber("distance", 0.0));
  aOut.loop = aRow.GetBool("loop", false);
  aOut.fadeIn = static_cast<float>(aRow.GetNumber("fadeIn", 0.0));
  aOut.fadeOut = static_cast<float>(aRow.GetNumber("fadeOut", 0.0));
  aOut.start = static_cast<float>(aRow.GetNumber("start", 0.0));
  aOut.end = static_cast<float>(aRow.GetNumber("end", 0.0));
  aOut.rate = static_cast<float>(aRow.GetNumber("rate", 1.0));
  aOut.stream = aRow.GetBool("stream", false);
  aOut.maxDuration = static_cast<float>(aRow.GetNumber("maxDuration", 0.0));
  if (aOut.gain < 0.0f) aOut.gain = 0.0f;
  if (aOut.rate <= 0.0f) aOut.rate = 1.0f;
  return true;
}

}  

int Manifest::LoadFile(SoundRegistry& aRegistry, const std::string& aPath) {
  auto* plugin = AudioXLPlugin::Get();
  std::ifstream f(aPath, std::ios::binary);
  if (!f) {
    plugin->Error("manifest " + aPath + ": cannot open");
    return 0;
  }
  std::stringstream ss;
  ss << f.rdbuf();
  JsonValue root;
  std::string err;
  if (!JsonParser::Parse(ss.str(), root, err)) {
    plugin->Error("manifest " + aPath + ": " + err);
    aRegistry.Note("manifest " + aPath + ": PARSE ERROR " + err);
    return 0;
  }
  
  const JsonValue* rows = root.Get("sounds");
  if (!rows) rows = root.Get("customSounds");
  if (!rows && root.IsArray()) rows = &root;
  if (!rows && root.Get("banks")) return 0;   
  if (!rows || !rows->IsArray()) {
    plugin->Error("manifest " + aPath + ": no 'sounds' (or REDmod 'customSounds') array");
    aRegistry.Note("manifest " + aPath + ": no sounds array");
    return 0;
  }
  const fs::path dir = fs::path(aPath).parent_path();
  const std::string modName = dir.filename().string();
  const std::string source = "manifest:" + modName;
  if (const JsonValue* banks = root.Get("banks")) {
    if (banks->IsArray()) {
      for (const auto& b : banks->arr) {
        if (b.IsString()) aRegistry.LoadBank((dir / b.str).lexically_normal().string(), source);
      }
    }
  }
  aRegistry.Note("manifest " + modName + " (" + std::to_string(rows->arr.size()) + " rows)");
  int ok = 0;
  int failed = 0;
  for (const auto& rowIn : rows->arr) {
    
    std::vector<JsonValue> expanded;
    const JsonValue* fileObj = rowIn.IsObject() ? rowIn.Get("file") : nullptr;
    if (fileObj && fileObj->IsObject()) {
      static const char* const kGender[][2] = {{"fem", "_f"}, {"female", "_f"}, {"male", "_m"}};
      for (const auto& g : kGender) {
        const JsonValue* f = fileObj->Get(g[0]);
        if (!f || !f->IsString()) continue;
        JsonValue copy = rowIn;
        copy.obj["file"] = *f;
        JsonValue nm;
        nm.kind = JsonValue::Kind::String;
        nm.str = rowIn.GetString("name") + g[1];
        copy.obj["name"] = nm;
        expanded.push_back(copy);
      }
    } else {
      expanded.push_back(rowIn);
    }
    for (const auto& row : expanded) {
    SoundSpec spec;
    std::string why;
    if (!RowToSpec(row, dir, spec, why)) {
      const std::string name = row.IsObject() ? row.GetString("name", "?") : "?";
      plugin->Error("manifest " + modName + ": row '" + name + "': " + why);
      aRegistry.Note("  FAILED " + name + ": " + why);
      ++failed;
      continue;
    }
    if (aRegistry.Register(spec, source)) {
      ++ok;
    } else {
      ++failed;
    }
    }
  }
  plugin->Info("manifest " + modName + ": " + std::to_string(ok) + " registered, " + std::to_string(failed) + " failed/skipped");
  aRegistry.Note("manifest " + modName + ": " + std::to_string(ok) + " registered, " + std::to_string(failed) + " failed/skipped");
  return ok;
}

void Manifest::LoadAll(SoundRegistry& aRegistry) {
  auto* plugin = AudioXLPlugin::Get();
  const fs::path root = fs::path(aRegistry.GameRoot()) / "red4ext" / "plugins" / "AudioXL" / "sounds";
  std::error_code ec;
  if (!fs::is_directory(root, ec)) {
    return;
  }
  int mods = 0;
  for (const auto& entry : fs::directory_iterator(root, ec)) {
    if (!entry.is_directory()) continue;
    fs::path manifest = entry.path() / "sounds.json";
    if (!fs::exists(manifest)) manifest = entry.path() / "info.json";
    if (!fs::exists(manifest)) continue;
    ++mods;
    LoadFile(aRegistry, manifest.string());
  }
  (void)mods;
}

}  
