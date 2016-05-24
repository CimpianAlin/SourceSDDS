#include "SourceSDDS_base.h"

/*******************************************************************************************

    AUTO-GENERATED CODE. DO NOT MODIFY

    The following class functions are for the base class for the component class. To
    customize any of these functions, do not modify them here. Instead, overload them
    on the child class

******************************************************************************************/

SourceSDDS_base::SourceSDDS_base(const char *uuid, const char *label) :
    Component(uuid, label),
    ThreadedComponent()
{
    loadProperties();

    dataSddsIn = new bulkio::InSDDSPort("dataSddsIn");
    addPort("dataSddsIn", dataSddsIn);
    dataOctetOut = new bulkio::OutOctetPort("dataOctetOut");
    addPort("dataOctetOut", dataOctetOut);
    dataShortOut = new bulkio::OutShortPort("dataShortOut");
    addPort("dataShortOut", dataShortOut);
    dataFloatOut = new bulkio::OutFloatPort("dataFloatOut");
    addPort("dataFloatOut", dataFloatOut);
}

SourceSDDS_base::~SourceSDDS_base()
{
    delete dataSddsIn;
    dataSddsIn = 0;
    delete dataOctetOut;
    dataOctetOut = 0;
    delete dataShortOut;
    dataShortOut = 0;
    delete dataFloatOut;
    dataFloatOut = 0;
}

/*******************************************************************************************
    Framework-level functions
    These functions are generally called by the framework to perform housekeeping.
*******************************************************************************************/
void SourceSDDS_base::start() throw (CORBA::SystemException, CF::Resource::StartError)
{
    Component::start();
    ThreadedComponent::startThread();
}

void SourceSDDS_base::stop() throw (CORBA::SystemException, CF::Resource::StopError)
{
    Component::stop();
    if (!ThreadedComponent::stopThread()) {
        throw CF::Resource::StopError(CF::CF_NOTSET, "Processing thread did not die");
    }
}

void SourceSDDS_base::releaseObject() throw (CORBA::SystemException, CF::LifeCycle::ReleaseError)
{
    // This function clears the component running condition so main shuts down everything
    try {
        stop();
    } catch (CF::Resource::StopError& ex) {
        // TODO - this should probably be logged instead of ignored
    }

    Component::releaseObject();
}

void SourceSDDS_base::loadProperties()
{
    addProperty(interface,
                "eth0",
                "interface",
                "interface",
                "readwrite",
                "",
                "external",
                "property");

    addProperty(advanced_optimizations,
                advanced_optimizations_struct(),
                "advanced_optimizations",
                "",
                "readwrite",
                "",
                "external",
                "property");

    addProperty(attachment_override,
                attachment_override_struct(),
                "attachment_override",
                "",
                "readwrite",
                "",
                "external",
                "property");

    addProperty(advanced_configuration,
                advanced_configuration_struct(),
                "advanced_configuration",
                "",
                "readwrite",
                "",
                "external",
                "property");

    addProperty(status,
                status_struct(),
                "status",
                "",
                "readonly",
                "",
                "external",
                "property");

}


