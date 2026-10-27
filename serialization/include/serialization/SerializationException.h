#pragma once

#include <stdexcept>
#include <string>

namespace serialization
{
	class SerializationException : public std::runtime_error
	{
	public:
		explicit SerializationException(std::string const& message)
			: std::runtime_error(message)
		{
		}
	};
}
