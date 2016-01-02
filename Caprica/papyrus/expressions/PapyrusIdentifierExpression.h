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
  PapyrusIdentifier identifier;

  PapyrusIdentifierExpression(const parser::PapyrusFileLocation& loc, const PapyrusIdentifier& id) : PapyrusExpression(loc), identifier(id) { }
  virtual ~PapyrusIdentifierExpression() override = default;

  virtual pex::PexValue generateLoad(pex::PexFile* file, pex::PexFunctionBuilder& bldr) const override {
    bldr << location;
    return identifier.generateLoad(file, bldr, pex::PexValue::Identifier(file->getString("self")));
  }

  virtual void semantic(PapyrusResolutionContext* ctx) override {
    identifier = ctx->resolveIdentifier(identifier);
  }

  virtual PapyrusType resultType() const override {
    return identifier.resultType();
  }
};

}}}
