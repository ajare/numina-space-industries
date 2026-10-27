#include "serialization/BinarySerializer.h"
#include "serialization/YamlSerializer.h"
#include "serialization/SerializationException.h"

#include <iostream>
#include <stdexcept>

namespace
{
	void require(bool value)
	{
		if (!value) throw std::runtime_error("Serialization consumer contract failed");
	}

	template<class Backend>
	void roundTrip()
	{
		auto output = Backend::toString();
		serialization::Serializer& writer = *output;
		writer.beginMap("document");
		writer.writeUint32("schemaVersion", 138);
		writer.beginArray("entries");
		writer.beginMap("");
		writer.writeString("id", std::string("store"));
		writer.writeDouble("quantity", 1.25);
		writer.endMap();
		writer.endArray();
		writer.endMap();
		writer.serialize();

		auto input = Backend::fromString(output->getSerializedString());
		input->deserialize();
		serialization::Serializer& reader = *input;
		reader.beginMap("document");
		require(reader.readUint32("schemaVersion") == 138);
		reader.beginArray("entries");
		require(reader.nextArrayItem());
		reader.beginMap("");
		require(reader.readString("id") == "store");
		require(reader.readDouble("quantity") == 1.25);
		bool refused = false;
		try { reader.readDouble("missing"); }
		catch (serialization::SerializationException const&) { refused = true; }
		require(refused);
		reader.endMap();
		require(!reader.nextArrayItem());
		reader.endArray();
		reader.endMap();
	}
}

int main()
{
	try
	{
		roundTrip<serialization::YamlSerializer>();
		roundTrip<serialization::BinarySerializer>();
		return 0;
	}
	catch (std::exception const& error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
