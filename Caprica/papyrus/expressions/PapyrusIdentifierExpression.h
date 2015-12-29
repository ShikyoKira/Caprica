#pragma once

#include <papyrus/PapyrusIdentifier.h>
#include <papyrus/expressions/PapyrusExpression.h>
#include <papyrus/parser/PapyrusFileLocation.h>

#include <pex/PexFile.h>
#include <pex/PexFunctionBuilder.h>
#include <pex/PexValue.h>

namespace caprica { namespace papyrus { namespace expressions {

struct PapyrusIdentifierExpression final : public PapyrusExpression
{
  PapyrusIdentifier identifier{ };

  PapyrusIdentifierExpression(parser::PapyrusFileLocation loc) : PapyrusExpression(loc) { }
  ~PapyrusIdentifierExpression() = default;

  virtual pex::PexValue generateLoad(pex::PexFile* file, pex::PexFunctionBuilder& bldr) const override {
    bldr << location;
    // TODO: Fix this. The identifier should be the one generating the load.
    return pex::PexValue::Identifier(file->getString(identifier.name));
  }

  virtual void semantic(PapyrusResolutionContext* ctx) override {
    identifier = ctx->resolveIdentifier(identifier);
  }

  virtual PapyrusType resultType() const override {
    return identifier.resultType();
  }
};

}}}