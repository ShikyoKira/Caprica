#pragma once

#include <papyrus/PapyrusObject.h>
#include <papyrus/PapyrusType.h>
#include <papyrus/expressions/PapyrusExpression.h>
#include <papyrus/parser/PapyrusFileLocation.h>

#include <pex/PexFile.h>
#include <pex/PexFunctionBuilder.h>
#include <pex/PexValue.h>

namespace caprica { namespace papyrus { namespace expressions {

struct PapyrusSelfExpression final : public PapyrusExpression
{
  // We do this this way because we can't
  // get the type in resultType() otherwise.
  PapyrusType type;

  PapyrusSelfExpression(const parser::PapyrusFileLocation& loc, const PapyrusType& tp) : PapyrusExpression(loc), type(tp) { }
  virtual ~PapyrusSelfExpression() override = default;

  virtual pex::PexValue generateLoad(pex::PexFile* file, pex::PexFunctionBuilder& bldr) const override {
    return pex::PexValue::Identifier(file->getString("self"));
  }

  virtual void semantic(PapyrusResolutionContext* ctx) override {
    type = ctx->resolveType(type);
    if (ctx->object != type.resolvedObject)
      ctx->fatalError("An error occured while resolving the self type!");
  }

  virtual PapyrusType resultType() const override {
    return type;
  }
};

}}}
