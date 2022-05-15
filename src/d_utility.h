#pragma once

#include <squirrel.h>

#include <string>

// Offers various convenience functions for fetching info from Squirrel and converting its data to strings for output

namespace rumDebugUtility
{
  std::string BuildInstanceDescription( HSQUIRRELVM i_pVM );
  std::string BuildTableDescription( HSQUIRRELVM i_pVM );

  // Searched the root table and stack for a matching variable name
  SQObject FindSymbol( HSQUIRRELVM i_pVM, const std::string& i_strVariable, uint32_t i_iLocalStackLevel );

  std::string FormatVariable( HSQUIRRELVM i_pVM, const SQInteger i_iIndex );
  std::string FormatVariable( HSQUIRRELVM i_pVM, HSQOBJECT i_sqObject );

  std::string GetObjectName( HSQUIRRELVM i_pVM, HSQOBJECT i_sqObject );
  std::string GetTypeName( SQObjectType i_eObjectType );

  bool IsOperator( const std::string& i_strToken );
  bool IsReservedWord( const std::string& i_strToken );
  bool IsUnknownType( SQObjectType i_eObjectType );
}
