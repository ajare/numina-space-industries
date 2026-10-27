#pragma once

#include <memory>
#include <string>

#include "serialization/Serializer.h"

namespace serialization
{
	class SerializerFactory
	{
	public:
		virtual ~SerializerFactory() = default;

		virtual std::unique_ptr<Serializer> create(std::string const& target) const = 0;
		virtual std::string description() const = 0;
		virtual std::string extension() const = 0;
	};
}
