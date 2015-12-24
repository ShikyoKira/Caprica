#pragma once

#include <string>
#include <vector>

#include <papyrus/PapyrusProperty.h>
#include <papyrus/PapyrusUserFlags.h>

#include <pex/PexDebugPropertyGroup.h>
#include <pex/PexFile.h>
#include <pex/PexObject.h>

namespace caprica { namespace papyrus {

struct PapyrusPropertyGroup final
{
  std::string name{ "" };
  std::string documentationComment{ "" };
  PapyrusUserFlags userFlags{ PapyrusUserFlags::None };
  std::vector<PapyrusProperty*> properties{ };

  PapyrusPropertyGroup() = default;
  ~PapyrusPropertyGroup() = default;

  void buildPex(pex::PexFile* file, pex::PexObject* obj) const {
    if (file->debugInfo) {
      auto pg = new pex::PexDebugPropertyGroup();
      pg->objectName = obj->name;
      pg->groupName = file->getString(name);
      pg->documentationString = file->getString(documentationComment);
      pg->userFlags = userFlags;
      for (auto p : properties)
        pg->properties.push_back(file->getString(p->name));
      file->debugInfo->propertyGroups.push_back(pg);
    }
  }
};

}}