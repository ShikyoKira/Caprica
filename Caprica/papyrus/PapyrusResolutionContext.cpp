#include <papyrus/PapyrusResolutionContext.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <boost/filesystem.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include <common/CapricaConfig.h>
#include <common/CapricaReportingContext.h>
#include <common/FSUtils.h>

#include <papyrus/PapyrusCustomEvent.h>
#include <papyrus/PapyrusFunction.h>
#include <papyrus/PapyrusObject.h>
#include <papyrus/PapyrusScript.h>
#include <papyrus/PapyrusStruct.h>
#include <papyrus/expressions/PapyrusCastExpression.h>
#include <papyrus/expressions/PapyrusExpression.h>
#include <papyrus/expressions/PapyrusLiteralExpression.h>
#include <papyrus/parser/PapyrusParser.h>
#include <papyrus/statements/PapyrusDeclareStatement.h>

#include <pex/PexReflector.h>
#include <pex/parser/PexAsmParser.h>

namespace caprica { namespace papyrus {

void PapyrusResolutionContext::addImport(const CapricaFileLocation& location, const std::string& import) {
  auto sc = loadScript(import);
  if (!sc)
    reportingContext.error(location, "Failed to find imported script '%s'!", import.c_str());
  for (auto s : importedScripts) {
    if (s == sc)
      reportingContext.error(location, "Duplicate import of '%s'.", import.c_str());
  }
  importedScripts.push_back(sc);
}

// This is safe because it will only ever contain scripts referencing items in this map, and this map
// will never contain a fully-resolved script.
static thread_local caseless_unordered_path_map<const std::string, std::unique_ptr<PapyrusScript>> loadedScripts{ };
static thread_local caseless_unordered_path_map<const std::string, caseless_unordered_identifier_map<const std::string, PapyrusScript*>> localPerDirIdentMap{ };
PapyrusScript* PapyrusResolutionContext::loadScript(const std::string& name) {
  auto baseDir = boost::filesystem::path(script->sourceFileName).parent_path().string();

  auto& sf2 = localPerDirIdentMap.find(baseDir);
  if (sf2 != localPerDirIdentMap.end()) {
    auto sf3 = sf2->second.find(name);
    if (sf3 != sf2->second.end()) {
      return sf3->second;
    }
  }

  const auto searchDir = [](const std::string& baseDir, const std::string& scriptName) -> PapyrusScript* {
    const auto loadPsc = [](const std::string& scriptName, const std::string& baseDir, const std::string& filename) -> PapyrusScript* {
      auto f = loadedScripts.find(filename);
      if (f != loadedScripts.end())
        return f->second.get();

      // We should only ever be searching for things in the root import dir,
      // so this is safe.
      CapricaReportingContext repCtx{ filename };
      auto parser = new parser::PapyrusParser(repCtx, filename);
      auto a = std::unique_ptr<PapyrusScript>(parser->parseScript());
      repCtx.exitIfErrors();
      delete parser;

      auto ctx = new PapyrusResolutionContext(repCtx);
      ctx->resolvingReferenceScript = true;
      a->preSemantic(ctx);

      if (!localPerDirIdentMap.count(baseDir))
        localPerDirIdentMap.insert({ baseDir, { } });
      localPerDirIdentMap[baseDir].insert({ scriptName, a.get() });
      loadedScripts.insert({ filename, std::move(a) });
      loadedScripts[filename]->semantic(ctx);
      repCtx.exitIfErrors();
      delete ctx;
      return loadedScripts[filename].get();
    };
    const auto loadPas = [](const std::string& scriptName, const std::string& baseDir, const std::string& filename) -> PapyrusScript* {
      auto f = loadedScripts.find(filename);
      if (f != loadedScripts.end())
        return f->second.get();

      CapricaReportingContext repCtx{ filename };
      auto parser = new pex::parser::PexAsmParser(repCtx, filename);
      auto pex = parser->parseFile();
      repCtx.exitIfErrors();
      delete parser;

      auto a = std::unique_ptr<PapyrusScript>(pex::PexReflector::reflectScript(pex));
      repCtx.exitIfErrors();
      delete pex;

      auto ctx = new PapyrusResolutionContext(repCtx);
      ctx->resolvingReferenceScript = true;
      ctx->isPexResolution = true;
      a->preSemantic(ctx);
      if (!localPerDirIdentMap.count(baseDir))
        localPerDirIdentMap.insert({ baseDir, { } });
      localPerDirIdentMap[baseDir].insert({ scriptName, a.get() });
      loadedScripts.insert({ filename, std::move(a) });
      loadedScripts[filename]->semantic(ctx);
      repCtx.exitIfErrors();
      delete ctx;

      return loadedScripts[filename].get();
    };
    const auto loadPex = [](const std::string& scriptName, const std::string& baseDir, const std::string& filename) -> PapyrusScript* {
      auto f = loadedScripts.find(filename);
      if (f != loadedScripts.end())
        return f->second.get();

      pex::PexReader rdr(filename);
      auto pex = pex::PexFile::read(rdr);
      auto a = std::unique_ptr<PapyrusScript>(pex::PexReflector::reflectScript(pex));
      delete pex;

      CapricaReportingContext repCtx{ filename };
      auto ctx = new PapyrusResolutionContext(repCtx);
      ctx->resolvingReferenceScript = true;
      ctx->isPexResolution = true;
      a->preSemantic(ctx);
      if (!localPerDirIdentMap.count(baseDir))
        localPerDirIdentMap.insert({ baseDir, { } });
      localPerDirIdentMap[baseDir].insert({ scriptName, a.get() });
      loadedScripts.insert({ filename, std::move(a) });
      loadedScripts[filename]->semantic(ctx);
      repCtx.exitIfErrors();
      delete ctx;

      return loadedScripts[filename].get();
    };
    const auto normalizeDir = [](const std::string& filename) -> std::string {
      return FSUtils::canonical(filename).string();
    };

    auto sr = FSUtils::multiExistsInDir(baseDir, { scriptName + ".psc", scriptName + ".pas", scriptName + ".pex" });
    if (sr[0])
      return loadPsc(scriptName, baseDir, normalizeDir(baseDir + "\\" + scriptName + ".psc"));
    else if (sr[1])
      return loadPas(scriptName, baseDir, normalizeDir(baseDir + "\\" + scriptName + ".pas"));
    else if (sr[2])
     return loadPex(scriptName, baseDir, normalizeDir(baseDir + "\\" + scriptName + ".pex"));

    return nullptr;
  };

  // Allow references to subdirs.
  auto nm2 = name;
  std::string extraBase = "";
  if (auto bPos = strrchr(nm2.c_str(), ':')) {
    extraBase = "\\" + nm2.substr(0, bPos - nm2.c_str());
    std::replace(extraBase.begin(), extraBase.end(), ':', '\\');
    nm2 = nm2.substr(bPos - nm2.c_str() + 1);
  }
  if (auto s = searchDir(baseDir + extraBase, nm2))
    return s;

  for (auto& dir : conf::Papyrus::importDirectories) {
    if (auto s = searchDir(dir + extraBase, nm2))
      return s;
  }

  return nullptr;
}

bool PapyrusResolutionContext::isObjectSomeParentOf(const PapyrusObject* child, const PapyrusObject* parent) {
  if (child == parent)
    return true;
  if (idEq(child->name, parent->name))
    return true;
  if (auto parentObject = child->tryGetParentClass())
    return isObjectSomeParentOf(parentObject, parent);
  return false;
}

bool PapyrusResolutionContext::canExplicitlyCast(const PapyrusType& src, const PapyrusType& dest) {
  if (canImplicitlyCoerce(src, dest))
    return true;

  if (src.type == PapyrusType::Kind::Var)
    return dest.type != PapyrusType::Kind::None;

  switch (dest.type) {
    case PapyrusType::Kind::Int:
    case PapyrusType::Kind::Float:
      switch (src.type) {
        case PapyrusType::Kind::String:
        case PapyrusType::Kind::Int:
        case PapyrusType::Kind::Float:
        case PapyrusType::Kind::Bool:
        case PapyrusType::Kind::Var:
          return true;
        default:
          return false;
      }

    case PapyrusType::Kind::ResolvedObject:
      if (src.type == PapyrusType::Kind::ResolvedObject)
        return isObjectSomeParentOf(dest.resolvedObject, src.resolvedObject);
      return false;
    case PapyrusType::Kind::Array:
      if (src.type == PapyrusType::Kind::Array && src.getElementType().type == PapyrusType::Kind::ResolvedObject && dest.getElementType().type == PapyrusType::Kind::ResolvedObject) {
        return isObjectSomeParentOf(dest.getElementType().resolvedObject, src.getElementType().resolvedObject);
      }
      return false;

    case PapyrusType::Kind::None:
    case PapyrusType::Kind::Bool:
    case PapyrusType::Kind::String:
    case PapyrusType::Kind::Var:
    case PapyrusType::Kind::CustomEventName:
    case PapyrusType::Kind::ScriptEventName:
    case PapyrusType::Kind::Unresolved:
    case PapyrusType::Kind::ResolvedStruct:
      return false;
  }
  CapricaReportingContext::logicalFatal("Unknown PapyrusTypeKind!");
}

bool PapyrusResolutionContext::canImplicitlyCoerce(const PapyrusType& src, const PapyrusType& dest) {
  if (src == dest)
    return true;

  switch (dest.type) {
    case PapyrusType::Kind::Bool:
      return src.type != PapyrusType::Kind::None;
    case PapyrusType::Kind::Float:
      return src.type == PapyrusType::Kind::Int;
    case PapyrusType::Kind::String:
      return src.type != PapyrusType::Kind::None;
    case PapyrusType::Kind::ResolvedObject:
      if (src.type == PapyrusType::Kind::ResolvedObject)
        return isObjectSomeParentOf(src.resolvedObject, dest.resolvedObject);
      return false;
    case PapyrusType::Kind::Var:
      return src.type != PapyrusType::Kind::None && src.type != PapyrusType::Kind::Array;
    case PapyrusType::Kind::None:
    case PapyrusType::Kind::Int:
    case PapyrusType::Kind::Array:
    case PapyrusType::Kind::Unresolved:
    case PapyrusType::Kind::ResolvedStruct:
    case PapyrusType::Kind::CustomEventName:
    case PapyrusType::Kind::ScriptEventName:
      return false;
  }
  CapricaReportingContext::logicalFatal("Unknown PapyrusTypeKind!");
}

bool PapyrusResolutionContext::canImplicitlyCoerceExpression(expressions::PapyrusExpression* expr, const PapyrusType& target) {
  switch (target.type) {
    case PapyrusType::Kind::Var:
    case PapyrusType::Kind::Array:
    case PapyrusType::Kind::ResolvedObject:
    case PapyrusType::Kind::ResolvedStruct:
      // Implicit conversion from None to each of these is allowed, but only for a literal None
      if (expr->resultType().type == PapyrusType::Kind::None && expr->is<expressions::PapyrusLiteralExpression>()) {
        return true;
      }
      // Deliberate Fallthrough

    case PapyrusType::Kind::Bool:
    case PapyrusType::Kind::Int:
    case PapyrusType::Kind::Float:
    case PapyrusType::Kind::String:
    case PapyrusType::Kind::CustomEventName:
    case PapyrusType::Kind::ScriptEventName:
    case PapyrusType::Kind::Unresolved:
    case PapyrusType::Kind::None:
      return canImplicitlyCoerce(expr->resultType(), target);
  }
  CapricaReportingContext::logicalFatal("Unknown PapyrusTypeKind!");
}

expressions::PapyrusExpression* PapyrusResolutionContext::coerceExpression(expressions::PapyrusExpression* expr, const PapyrusType& target) const {
  if (expr->resultType() != target) {
    bool canCast = canImplicitlyCoerceExpression(expr, target);

    if (canCast && expr->resultType().type == PapyrusType::Kind::Int && target.type == PapyrusType::Kind::Float) {
      if (auto le = expr->as<expressions::PapyrusLiteralExpression>()) {
        le->value.f = (float)le->value.i;
        le->value.type = PapyrusValueType::Float;
        return expr;
      }
    }

    if (!canCast) {
      reportingContext.error(expr->location, "No implicit conversion from '%s' to '%s' exists!", expr->resultType().prettyString().c_str(), target.prettyString().c_str());
      return expr;
    }
    auto ce = new expressions::PapyrusCastExpression(expr->location, target);
    ce->innerExpression = expr;
    return ce;
  }
  return expr;
}

PapyrusValue PapyrusResolutionContext::coerceDefaultValue(const PapyrusValue& val, const PapyrusType& target) const {
  if (val.type == PapyrusValueType::Invalid || val.getPapyrusType() == target)
    return val;

  switch (target.type) {
    case PapyrusType::Kind::Float:
      if (val.getPapyrusType().type == PapyrusType::Kind::Int && target.type == PapyrusType::Kind::Float)
        return PapyrusValue::Float(val.location, (float)val.i);
      break;
    case PapyrusType::Kind::Array:
    case PapyrusType::Kind::ResolvedObject:
    case PapyrusType::Kind::ResolvedStruct:
      if (val.getPapyrusType().type == PapyrusType::Kind::None)
        return val;
      break;
    default:
      break;
  }
  reportingContext.error(val.location, "Cannot initialize a '%s' value with a '%s'!", target.prettyString().c_str(), val.getPapyrusType().prettyString().c_str());
  return val;
}

void PapyrusResolutionContext::checkForPoison(const expressions::PapyrusExpression* expr) const {
  checkForPoison(expr->resultType());
}

void PapyrusResolutionContext::checkForPoison(const PapyrusType& type) const {
  if (type.isPoisoned(PapyrusType::PoisonKind::Beta)) {
    if (function != nullptr && function->isBetaOnly())
      goto CheckDebug;
    if (object != nullptr && object->isBetaOnly())
      goto CheckDebug;
    reportingContext.error(type.location, "You cannot use the return value of a BetaOnly function in a non-BetaOnly context!");
    return;
  }
CheckDebug:
  if (type.isPoisoned(PapyrusType::PoisonKind::Debug)) {
    if (function != nullptr && function->isDebugOnly())
      return;
    if (object != nullptr && object->isDebugOnly())
      return;
    reportingContext.error(type.location, "You cannot use the return value of a DebugOnly function in a non-DebugOnly context!");
    return;
  }
}

PapyrusFunction* PapyrusResolutionContext::tryResolveEvent(const PapyrusObject* parentObj, const std::string& name) const {
  for (auto f : parentObj->getRootState()->functions) {
    if (idEq(f->name, name) && f->functionType == PapyrusFunctionType::Event)
      return f;
  }

  if (auto parentClass = parentObj->tryGetParentClass())
    return tryResolveEvent(parentClass, name);

  return nullptr;
}

PapyrusCustomEvent* PapyrusResolutionContext::tryResolveCustomEvent(const PapyrusObject* parentObj, const std::string& name) const {
  for (auto c : parentObj->customEvents) {
    if (idEq(c->name, name))
      return c;
  }

  if (auto parentClass = parentObj->tryGetParentClass())
    return tryResolveCustomEvent(parentClass, name);

  return nullptr;
}

PapyrusState* PapyrusResolutionContext::tryResolveState(const std::string& name, const PapyrusObject* parentObj) const {
  if (!parentObj)
    parentObj = object;

  for (auto s : parentObj->states) {
    if (idEq(s->name, name))
      return s;
  }

  if (auto parentClass = parentObj->tryGetParentClass())
    return tryResolveState(name, parentClass);

  return nullptr;
}

static PapyrusType tryResolveStruct(const PapyrusObject* object, PapyrusType tp) {
  for (auto& s : object->structs) {
    if (idEq(s->name, tp.name)) {
      tp.type = PapyrusType::Kind::ResolvedStruct;
      tp.resolvedStruct = s;
      return tp;
    }
  }
  
  if (auto parentClass = object->tryGetParentClass())
    return tryResolveStruct(parentClass, tp);

  return tp;
}

PapyrusType PapyrusResolutionContext::resolveType(PapyrusType tp) {
  if (tp.type != PapyrusType::Kind::Unresolved) {
    if (tp.type == PapyrusType::Kind::Array)
      return PapyrusType::Array(tp.location, std::make_shared<PapyrusType>(resolveType(tp.getElementType())));
    return tp;
  }

  if (isPexResolution || conf::Papyrus::allowDecompiledStructNameRefs) {
    auto pos = tp.name.find_first_of('#');
    if (pos != std::string::npos) {
      auto scName = tp.name.substr(0, pos);
      auto strucName = tp.name.substr(pos + 1);
      auto sc = loadScript(scName);
      if (!sc)
        reportingContext.fatal(tp.location, "Unable to find script '%s' referenced by '%s'!", scName.c_str(), tp.name.c_str());

      for (auto obj : sc->objects) {
        for (auto struc : obj->structs) {
          if (idEq(struc->name, strucName)) {
            tp.type = PapyrusType::Kind::ResolvedStruct;
            tp.resolvedStruct = struc;
            return tp;
          }
        }
      }

      reportingContext.fatal(tp.location, "Unable to resolve a struct named '%s' in script '%s'!", strucName.c_str(), scName.c_str());
    }
  }

  if (object) {
    auto t2 = tryResolveStruct(object, tp);
    if (t2.type != PapyrusType::Kind::Unresolved)
      return t2;

    if (idEq(object->name, tp.name)) {
      tp.type = PapyrusType::Kind::ResolvedObject;
      tp.resolvedObject = object;
      return tp;
    }
  }

  for (auto sc : importedScripts) {
    for (auto obj : sc->objects) {
      for (auto struc : obj->structs) {
        if (idEq(struc->name, tp.name)) {
          tp.type = PapyrusType::Kind::ResolvedStruct;
          tp.resolvedStruct = struc;
          return tp;
        }
      }
    }
  }

  auto sc = loadScript(tp.name);
  if (sc != nullptr) {
    for (auto obj : sc->objects) {
      auto oName = obj->name;
      auto pos = oName.find_last_of(':');
      if (pos != std::string::npos)
        oName = oName.substr(pos + 1);
      if (idEq(obj->name, tp.name) || idEq(oName, tp.name)) {
        tp.type = PapyrusType::Kind::ResolvedObject;
        tp.resolvedObject = obj;
        return tp;
      }
    }
    reportingContext.fatal(tp.location, "Loaded a script named '%s' but was looking for '%s'!", sc->objects[0]->name.c_str(), tp.name.c_str());
  }
  
  auto pos = tp.name.find_last_of(':');
  if (pos != std::string::npos) {
    auto scName = tp.name.substr(0, pos);
    auto strucName = tp.name.substr(pos + 1);
    auto sc = loadScript(scName);
    if (!sc)
      reportingContext.fatal(tp.location, "Unable to find script '%s' referenced by '%s'!", scName.c_str(), tp.name.c_str());

    for (auto obj : sc->objects) {
      for (auto struc : obj->structs) {
        if (idEq(struc->name, strucName)) {
          tp.type = PapyrusType::Kind::ResolvedStruct;
          tp.resolvedStruct = struc;
          return tp;
        }
      }
    }

    reportingContext.fatal(tp.location, "Unable to resolve a struct named '%s' in script '%s'!", strucName.c_str(), scName.c_str());
  }

  reportingContext.fatal(tp.location, "Unable to resolve type '%s'!", tp.name.c_str());
}

void PapyrusResolutionContext::addLocalVariable(statements::PapyrusDeclareStatement* local) {
  for (auto is : localVariableScopeStack) {
    if (is.count(local->name)) {
      reportingContext.error(local->location, "Attempted to redefined '%s' which was already defined in a parent scope!", local->name.c_str());
      return;
    }
  }
  localVariableScopeStack.back().insert({ local->name, local });
}

PapyrusIdentifier PapyrusResolutionContext::resolveIdentifier(const PapyrusIdentifier& ident) const {
  auto id = tryResolveIdentifier(ident);
  if (id.type == PapyrusIdentifierType::Unresolved)
    reportingContext.fatal(ident.location, "Unresolved identifier '%s'!", ident.name.c_str());
  return id;
}

PapyrusIdentifier PapyrusResolutionContext::tryResolveIdentifier(const PapyrusIdentifier& ident) const {
  if (ident.type != PapyrusIdentifierType::Unresolved)
    return ident;

  // This handles local var resolution.
  for (auto& stack : boost::adaptors::reverse(localVariableScopeStack)) {
    auto f = stack.find(ident.name);
    if (f != stack.end()) {
      return PapyrusIdentifier::DeclStatement(ident.location, f->second);
    }
  }

  if (function) {
    if ((idEq(function->name, "getstate") || idEq(function->name, "gotostate")) && idEq(ident.name, "__state")) {
      auto i = ident;
      i.type = PapyrusIdentifierType::BuiltinStateField;
      return i;
    }

    for (auto p : function->parameters) {
      if (idEq(p->name, ident.name))
        return PapyrusIdentifier::FunctionParameter(ident.location, p);
    }
  }

  if (!function || !function->isGlobal()) {
    for (auto v : object->variables) {
      if (idEq(v->name, ident.name))
        return PapyrusIdentifier::Variable(ident.location, v);
    }

    for (auto pg : object->propertyGroups) {
      for (auto p : pg->properties) {
        if (idEq(p->name, ident.name))
          return PapyrusIdentifier::Property(ident.location, p);
      }
    }
  }

  if (auto parentClass = object->tryGetParentClass())
    return tryResolveMemberIdentifier(object->parentClass, ident);

  return ident;
}

PapyrusIdentifier PapyrusResolutionContext::resolveMemberIdentifier(const PapyrusType& baseType, const PapyrusIdentifier& ident) const {
  auto id = tryResolveMemberIdentifier(baseType, ident);
  if (id.type == PapyrusIdentifierType::Unresolved)
    reportingContext.fatal(ident.location, "Unresolved identifier '%s'!", ident.name.c_str());
  return id;
}

PapyrusIdentifier PapyrusResolutionContext::tryResolveMemberIdentifier(const PapyrusType& baseType, const PapyrusIdentifier& ident) const {
  if (ident.type != PapyrusIdentifierType::Unresolved)
    return ident;

  if (baseType.type == PapyrusType::Kind::ResolvedStruct) {
    for (auto& sm : baseType.resolvedStruct->members) {
      if (idEq(sm->name, ident.name))
        return PapyrusIdentifier::StructMember(ident.location, sm);
    }
  } else if (baseType.type == PapyrusType::Kind::ResolvedObject) {
    for (auto& propGroup : baseType.resolvedObject->propertyGroups) {
      for (auto& prop : propGroup->properties) {
        if (idEq(prop->name, ident.name))
          return PapyrusIdentifier::Property(ident.location, prop);
      }
    }

    if (auto parentClass = baseType.resolvedObject->tryGetParentClass())
      return tryResolveMemberIdentifier(baseType.resolvedObject->parentClass, ident);
  }

  return ident;
}

PapyrusIdentifier PapyrusResolutionContext::resolveFunctionIdentifier(const PapyrusType& baseType, const PapyrusIdentifier& ident, bool wantGlobal) const {
  auto id = tryResolveFunctionIdentifier(baseType, ident, wantGlobal);
  if (id.type == PapyrusIdentifierType::Unresolved)
    reportingContext.fatal(ident.location, "Unresolved function name '%s'!", ident.name.c_str());
  return id;
}

PapyrusIdentifier PapyrusResolutionContext::tryResolveFunctionIdentifier(const PapyrusType& baseType, const PapyrusIdentifier& ident, bool wantGlobal) const {
  wantGlobal = wantGlobal || (function && function->isGlobal());
  if (ident.type != PapyrusIdentifierType::Unresolved)
    return ident;

  if (baseType.type == PapyrusType::Kind::None) {
    if (auto state = object->getRootState()) {
      for (auto& func : state->functions) {
        if (idEq(func->name, ident.name)) {
          if (wantGlobal && !func->isGlobal())
            reportingContext.error(ident.location, "You cannot call non-global functions from within a global function. '%s' is not a global function.", func->name.c_str());
          return PapyrusIdentifier::Function(ident.location, func);
        }
      }
    }

    for (auto sc : importedScripts) {
      for (auto obj : sc->objects) {
        if (auto state = obj->getRootState()) {
          for (auto& func : state->functions) {
            if (func->isGlobal() && idEq(func->name, ident.name))
              return PapyrusIdentifier::Function(ident.location, func);
          }
        }
      }
    }

    return tryResolveFunctionIdentifier(PapyrusType::ResolvedObject(ident.location, object), ident, wantGlobal);
  } else if (baseType.type == PapyrusType::Kind::Array) {
    auto fk = PapyrusBuiltinArrayFunctionKind::Unknown;
    if (idEq(ident.name, "find")) {
      fk = PapyrusBuiltinArrayFunctionKind::Find;
    } else if(idEq(ident.name, "findstruct")) {
      fk = PapyrusBuiltinArrayFunctionKind::FindStruct;
    } else if (idEq(ident.name, "rfind")) {
      fk = PapyrusBuiltinArrayFunctionKind::RFind;
    } else if (idEq(ident.name, "rfindstruct")) {
      fk = PapyrusBuiltinArrayFunctionKind::RFindStruct;
    } else if (idEq(ident.name, "add")) {
      fk = PapyrusBuiltinArrayFunctionKind::Add;
    } else if (idEq(ident.name, "clear")) {
      fk = PapyrusBuiltinArrayFunctionKind::Clear;
    } else if (idEq(ident.name, "insert")) {
      fk = PapyrusBuiltinArrayFunctionKind::Insert;
    } else if (idEq(ident.name, "remove")) {
      fk = PapyrusBuiltinArrayFunctionKind::Remove;
    } else if (idEq(ident.name, "removelast")) {
      fk = PapyrusBuiltinArrayFunctionKind::RemoveLast;
    } else {
      reportingContext.fatal(ident.location, "Unknown function '%s' called on an array expression!", ident.name.c_str());
    }
    return PapyrusIdentifier::ArrayFunction(baseType.location, fk, baseType.getElementType());
  } else if (baseType.type == PapyrusType::Kind::ResolvedObject) {
    if (auto state = baseType.resolvedObject->getRootState()) {
      for (auto& func : state->functions) {
        if (idEq(func->name, ident.name)) {
          if (!wantGlobal && func->isGlobal())
            reportingContext.error(ident.location, "You cannot call the global function '%s' on an object.", func->name.c_str());
          return PapyrusIdentifier::Function(ident.location, func);
        }
      }
    }

    if (auto parentClass = baseType.resolvedObject->tryGetParentClass())
      return tryResolveFunctionIdentifier(baseType.resolvedObject->parentClass, ident, wantGlobal);
  }

  return ident;
}

}}
