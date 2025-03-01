// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "AppInstallerSHA256.h"
#include "winget/ManifestYamlPopulator.h"

namespace AppInstaller::Manifest
{
    using ValidationErrors = std::vector<ValidationError>;

    namespace
    {
        // Only used in preview manifest
        std::vector<Manifest::string_t> SplitMultiValueField(const std::string& input)
        {
            if (input.empty())
            {
                return {};
            }

            std::vector<Manifest::string_t> result;
            size_t currentPos = 0;
            while (currentPos < input.size())
            {
                size_t splitPos = input.find(',', currentPos);
                if (splitPos == std::string::npos)
                {
                    splitPos = input.size();
                }

                std::string splitVal = input.substr(currentPos, splitPos - currentPos);
                Utility::Trim(splitVal);
                if (!splitVal.empty())
                {
                    result.emplace_back(std::move(splitVal));
                }
                currentPos = splitPos + 1;
            }

            return result;
        }

        std::vector<Manifest::string_t> ProcessStringSequenceNode(const YAML::Node& node, bool trim = true)
        {
            THROW_HR_IF(E_INVALIDARG, !node.IsSequence());

            std::vector<Manifest::string_t> result;

            for (auto const& entry : node.Sequence())
            {
                std::string value = entry.as<std::string>();
                if (trim)
                {
                    Utility::Trim(value);
                }

                result.emplace_back(std::move(value));
            }

            return result;
        }

        std::vector<DWORD> ProcessInstallerSuccessCodeSequenceNode(const YAML::Node& node)
        {
            THROW_HR_IF(E_INVALIDARG, !node.IsSequence());

            std::vector<DWORD> result;

            for (auto const& entry : node.Sequence())
            {
                result.emplace_back(static_cast<DWORD>(entry.as<int>()));
            }

            return result;
        }

        std::vector<PlatformEnum> ProcessPlatformSequenceNode(const YAML::Node& node)
        {
            THROW_HR_IF(E_INVALIDARG, !node.IsSequence());

            std::vector<PlatformEnum> result;

            for (auto const& entry : node.Sequence())
            {
                result.emplace_back(ConvertToPlatformEnum(entry.as<std::string>()));
            }

            return result;
        }

        std::vector<InstallModeEnum> ProcessInstallModeSequenceNode(const YAML::Node& node)
        {
            THROW_HR_IF(E_INVALIDARG, !node.IsSequence());

            std::vector<InstallModeEnum> result;

            for (auto const& entry : node.Sequence())
            {
                result.emplace_back(ConvertToInstallModeEnum(entry.as<std::string>()));
            }

            return result;
        }

        std::vector<AppInstaller::Utility::Architecture> ProcessArchitectureSequenceNode(const YAML::Node& node)
        {
            THROW_HR_IF(E_INVALIDARG, !node.IsSequence());

            std::vector<AppInstaller::Utility::Architecture> result;

            for (auto const& entry : node.Sequence())
            {
                result.emplace_back(Utility::ConvertToArchitectureEnum(entry.as<std::string>()));
            }

            return result;
        }
    }

    std::vector<ManifestYamlPopulator::FieldProcessInfo> ManifestYamlPopulator::GetRootFieldProcessInfo(const ManifestVer& manifestVersion)
    {
        // Common fields across versions
        std::vector<FieldProcessInfo> result =
        {
            { "ManifestVersion", [](const YAML::Node&)->ValidationErrors { /* ManifestVersion already populated. Field listed here for duplicate and PascalCase check */ return {}; } },
            { "Installers", [this](const YAML::Node& value)->ValidationErrors { m_p_installersNode = &value; return {}; } },
            { "Localization", [this](const YAML::Node& value)->ValidationErrors { m_p_localizationsNode = &value; return {}; } },
            { "Channel", [this](const YAML::Node& value)->ValidationErrors { m_p_manifest->Channel = Utility::Trim(value.as<std::string>()); return {}; } },
        };

        // Additional version specific fields
        if (manifestVersion.Major() == 0)
        {
            std::vector<FieldProcessInfo> previewRootFields
            {
                { "Id", [this](const YAML::Node& value)->ValidationErrors { m_p_manifest->Id = Utility::Trim(value.as<std::string>()); return {}; } },
                { "Version", [this](const YAML::Node& value)->ValidationErrors { m_p_manifest->Version = Utility::Trim(value.as<std::string>()); return {}; } },
                { "AppMoniker", [this](const YAML::Node& value)->ValidationErrors {  m_p_manifest->Moniker = Utility::Trim(value.as<std::string>()); return {}; } },
            };

            std::move(previewRootFields.begin(), previewRootFields.end(), std::inserter(result, result.end()));
        }
        else if (manifestVersion.Major() == 1)
        {
            // Starting v1, we should be only adding new fields for each minor version increase
            if (manifestVersion >= ManifestVer{ s_ManifestVersionV1 })
            {
                std::vector<FieldProcessInfo> v1RootFields
                {
                    { "PackageIdentifier", [this](const YAML::Node& value)->ValidationErrors { m_p_manifest->Id = Utility::Trim(value.as<std::string>()); return {}; } },
                    { "PackageVersion", [this](const YAML::Node& value)->ValidationErrors { m_p_manifest->Version = Utility::Trim(value.as<std::string>()); return {}; } },
                    { "Moniker", [this](const YAML::Node& value)->ValidationErrors {  m_p_manifest->Moniker = Utility::Trim(value.as<std::string>()); return {}; } },
                    { "ManifestType", [](const YAML::Node&)->ValidationErrors { /* ManifestType already checked. Field listed here for duplicate and PascalCase check */ return {}; } },
                };

                std::move(v1RootFields.begin(), v1RootFields.end(), std::inserter(result, result.end()));
            }
        }

        // Root fields mapped as Installer and Localization values
        auto rootInstallerFields = GetInstallerFieldProcessInfo(manifestVersion, true);
        std::move(rootInstallerFields.begin(), rootInstallerFields.end(), std::inserter(result, result.end()));

        auto rootLocalizationFields = GetLocalizationFieldProcessInfo(manifestVersion, true);
        std::move(rootLocalizationFields.begin(), rootLocalizationFields.end(), std::inserter(result, result.end()));

        return result;
    }

    std::vector<ManifestYamlPopulator::FieldProcessInfo> ManifestYamlPopulator::GetInstallerFieldProcessInfo(const ManifestVer& manifestVersion, bool forRootFields)
    {
        // Common fields across versions
        std::vector<FieldProcessInfo> result =
        {
            { "InstallerType", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->InstallerType = ConvertToInstallerTypeEnum(value.as<std::string>()); return {}; } },
            { "PackageFamilyName", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->PackageFamilyName = value.as<std::string>(); return {}; } },
            { "ProductCode", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->ProductCode = value.as<std::string>(); return {}; } },
        };

        // Additional version specific fields
        if (manifestVersion.Major() == 0)
        {
            // Root level and Localization node level
            std::vector<FieldProcessInfo> previewCommonFields =
            {
                { "UpdateBehavior", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->UpdateBehavior = ConvertToUpdateBehaviorEnum(value.as<std::string>()); return {}; } },
                { "Switches", [this](const YAML::Node& value)->ValidationErrors { m_p_switches = &(m_p_installer->Switches); return ValidateAndProcessFields(value, SwitchesFieldInfos); } },
            };

            std::move(previewCommonFields.begin(), previewCommonFields.end(), std::inserter(result, result.end()));

            if (!forRootFields)
            {
                // Installer node only
                std::vector<FieldProcessInfo> installerOnlyFields =
                {
                    { "Arch", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Arch = Utility::ConvertToArchitectureEnum(value.as<std::string>()); return {}; } },
                    { "Url", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Url = value.as<std::string>(); return {}; } },
                    { "Sha256", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Sha256 = Utility::SHA256::ConvertToBytes(value.as<std::string>()); return {}; } },
                    { "SignatureSha256", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->SignatureSha256 = Utility::SHA256::ConvertToBytes(value.as<std::string>()); return {}; } },
                    { "Language", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Locale = value.as<std::string>(); return {}; } },
                    { "Scope", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Scope = ConvertToScopeEnum(value.as<std::string>()); return {}; } },
                };

                if (manifestVersion.HasExtension(s_MSStoreExtension))
                {
                    installerOnlyFields.emplace_back("ProductId", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->ProductId = value.as<std::string>(); return {}; });
                }

                std::move(installerOnlyFields.begin(), installerOnlyFields.end(), std::inserter(result, result.end()));
            }
            else
            {
                // Root node only
                std::vector<FieldProcessInfo> rootOnlyFields =
                {
                    { "MinOSVersion", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->MinOSVersion = value.as<std::string>(); return {}; } },
                    { "Commands", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Commands = SplitMultiValueField(value.as<std::string>()); return {}; } },
                    { "Protocols", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Protocols = SplitMultiValueField(value.as<std::string>()); return {}; } },
                    { "FileExtensions", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->FileExtensions = SplitMultiValueField(value.as<std::string>()); return {}; } },
                };

                std::move(rootOnlyFields.begin(), rootOnlyFields.end(), std::inserter(result, result.end()));
            }
        }
        else if (manifestVersion.Major() == 1)
        {
            // Starting v1, we should be only adding new fields for each minor version increase
            if (manifestVersion >= ManifestVer{ s_ManifestVersionV1 })
            {
                // Root level and Installer node level
                std::vector<FieldProcessInfo> v1CommonFields =
                {
                    { "InstallerLocale", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Locale = value.as<std::string>(); return {}; } },
                    { "Platform", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Platform = ProcessPlatformSequenceNode(value); return {}; } },
                    { "MinimumOSVersion", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->MinOSVersion = value.as<std::string>(); return {}; } },
                    { "Scope", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Scope = ConvertToScopeEnum(value.as<std::string>()); return {}; } },
                    { "InstallModes", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->InstallModes = ProcessInstallModeSequenceNode(value); return {}; } },
                    { "InstallerSwitches", [this](const YAML::Node& value)->ValidationErrors { m_p_switches = &(m_p_installer->Switches); return ValidateAndProcessFields(value, SwitchesFieldInfos); } },
                    { "InstallerSuccessCodes", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->InstallerSuccessCodes = ProcessInstallerSuccessCodeSequenceNode(value); return {}; } },
                    { "UpgradeBehavior", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->UpdateBehavior = ConvertToUpdateBehaviorEnum(value.as<std::string>()); return {}; } },
                    { "Commands", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Commands = ProcessStringSequenceNode(value); return {}; } },
                    { "Protocols", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Protocols = ProcessStringSequenceNode(value); return {}; } },
                    { "FileExtensions", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->FileExtensions = ProcessStringSequenceNode(value); return {}; } },
                    { "Dependencies", [this](const YAML::Node& value)->ValidationErrors { m_p_dependencyList = &(m_p_installer->Dependencies); return ValidateAndProcessFields(value, DependenciesFieldInfos); } },
                    { "Capabilities", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Capabilities = ProcessStringSequenceNode(value); return {}; } },
                    { "RestrictedCapabilities", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->RestrictedCapabilities = ProcessStringSequenceNode(value); return {}; } },
                };

                std::move(v1CommonFields.begin(), v1CommonFields.end(), std::inserter(result, result.end()));

                if (!forRootFields)
                {
                    // Installer level only fields
                    std::vector<FieldProcessInfo> v1InstallerFields =
                    {
                        { "Architecture", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Arch = Utility::ConvertToArchitectureEnum(value.as<std::string>()); return {}; } },
                        { "InstallerUrl", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Url = value.as<std::string>(); return {}; } },
                        { "InstallerSha256", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->Sha256 = Utility::SHA256::ConvertToBytes(value.as<std::string>()); return {}; } },
                        { "SignatureSha256", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->SignatureSha256 = Utility::SHA256::ConvertToBytes(value.as<std::string>()); return {}; } },
                    };

                    std::move(v1InstallerFields.begin(), v1InstallerFields.end(), std::inserter(result, result.end()));
                }
            }

            if (manifestVersion >= ManifestVer{ s_ManifestVersionV1_1 })
            {
                std::vector<FieldProcessInfo> fields_v1_1 =
                {
                    { "InstallerAbortsTerminal", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->InstallerAbortsTerminal = value.as<bool>(); return {}; } },
                    { "InstallLocationRequired", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->InstallLocationRequired = value.as<bool>(); return {}; } },
                    { "RequireExplicitUpgrade", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->RequireExplicitUpgrade = value.as<bool>(); return {}; } },
                    { "ReleaseDate", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->ReleaseDate = Utility::Trim(value.as<std::string>()); return {}; } },
                    { "UnsupportedOSArchitectures", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->UnsupportedOSArchitectures = ProcessArchitectureSequenceNode(value); return {}; } },
                    { "ElevationRequirement", [this](const YAML::Node& value)->ValidationErrors { m_p_installer->ElevationRequirement = ConvertToElevationRequirementEnum(value.as<std::string>()); return {}; } },
                    { "Markets", [this](const YAML::Node& value)->ValidationErrors { return ProcessMarketsNode(value); } },
                    { "AppsAndFeaturesEntries", [this](const YAML::Node& value)->ValidationErrors { return ProcessAppsAndFeaturesEntriesNode(value); } },
                    { "ExpectedReturnCodes", [this](const YAML::Node& value)->ValidationErrors { return ProcessExpectedReturnCodesNode(value); } },
                };

                std::move(fields_v1_1.begin(), fields_v1_1.end(), std::inserter(result, result.end()));
            }
        }

        return result;
    }

    std::vector<ManifestYamlPopulator::FieldProcessInfo> ManifestYamlPopulator::GetSwitchesFieldProcessInfo(const ManifestVer& manifestVersion)
    {
        // Common fields across versions
        std::vector<FieldProcessInfo> result =
        {
            { "Custom", [this](const YAML::Node& value)->ValidationErrors { (*m_p_switches)[InstallerSwitchType::Custom] = value.as<std::string>(); return{}; } },
            { "Silent", [this](const YAML::Node& value)->ValidationErrors { (*m_p_switches)[InstallerSwitchType::Silent] = value.as<std::string>(); return{}; } },
            { "SilentWithProgress", [this](const YAML::Node& value)->ValidationErrors { (*m_p_switches)[InstallerSwitchType::SilentWithProgress] = value.as<std::string>(); return{}; } },
            { "Interactive", [this](const YAML::Node& value)->ValidationErrors { (*m_p_switches)[InstallerSwitchType::Interactive] = value.as<std::string>(); return{}; } },
            { "Log", [this](const YAML::Node& value)->ValidationErrors { (*m_p_switches)[InstallerSwitchType::Log] = value.as<std::string>(); return{}; } },
            { "InstallLocation", [this](const YAML::Node& value)->ValidationErrors { (*m_p_switches)[InstallerSwitchType::InstallLocation] = value.as<std::string>(); return{}; } },
        };

        // Additional version specific fields
        if (manifestVersion.Major() == 0)
        {
            // Language only exists in preview manifests. Though we don't use it in our code yet, keep it here to be consistent with schema.
            result.emplace_back("Language", [this](const YAML::Node& value)->ValidationErrors { (*m_p_switches)[InstallerSwitchType::Language] = value.as<std::string>(); return{}; });
            result.emplace_back("Update", [this](const YAML::Node& value)->ValidationErrors { (*m_p_switches)[InstallerSwitchType::Update] = value.as<std::string>(); return{}; });
        }
        else if (manifestVersion.Major() == 1)
        {
            result.emplace_back("Upgrade", [this](const YAML::Node& value)->ValidationErrors { (*m_p_switches)[InstallerSwitchType::Update] = value.as<std::string>(); return{}; });
        }

        return result;
    }

    std::vector<ManifestYamlPopulator::FieldProcessInfo> ManifestYamlPopulator::GetExpectedReturnCodesFieldProcessInfo(const ManifestVer& manifestVersion)
    {
        std::vector<FieldProcessInfo> result = {};

        if (manifestVersion >= ManifestVer{ s_ManifestVersionV1_1 })
        {
            result.emplace_back("InstallerReturnCode", [this](const YAML::Node& value)->ValidationErrors { m_p_expectedReturnCode->InstallerReturnCode = static_cast<int>(value.as<int>()); return {}; });
            result.emplace_back("ReturnResponse", [this](const YAML::Node& value)->ValidationErrors { m_p_expectedReturnCode->ReturnResponse = ConvertToExpectedReturnCodeEnum(value.as<std::string>()); return {}; });
        }

        return result;
    }

    std::vector<ManifestYamlPopulator::FieldProcessInfo> ManifestYamlPopulator::GetLocalizationFieldProcessInfo(const ManifestVer& manifestVersion, bool forRootFields)
    {
        // Common fields across versions
        std::vector<FieldProcessInfo> result =
        {
            { "Description", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::Description>(Utility::Trim(value.as<std::string>())); return {}; } },
            { "LicenseUrl", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::LicenseUrl>(value.as<std::string>()); return {}; } },
        };

        // Additional version specific fields
        if (manifestVersion.Major() == 0)
        {
            // Root level and Localization node level
            result.emplace_back("Homepage", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::PackageUrl>(value.as<std::string>()); return {}; });

            if (!forRootFields)
            {
                // Localization node only
                result.emplace_back("Language", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Locale = value.as<std::string>(); return {}; });
            }
            else
            {
                // Root node only
                std::vector<FieldProcessInfo> rootOnlyFields =
                {
                    { "Name", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::PackageName>(Utility::Trim(value.as<std::string>())); return {}; } },
                    { "Publisher", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::Publisher>(value.as<std::string>()); return {}; } },
                    { "Author", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::Author>(value.as<std::string>()); return {}; } },
                    { "License", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::License>(value.as<std::string>()); return {}; } },
                    { "Tags", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::Tags>(SplitMultiValueField(value.as<std::string>())); return {}; } },
                };

                std::move(rootOnlyFields.begin(), rootOnlyFields.end(), std::inserter(result, result.end()));
            }
        }
        else if (manifestVersion.Major() == 1)
        {
            // Starting v1, we should be only adding new fields for each minor version increase
            if (manifestVersion >= ManifestVer{ s_ManifestVersionV1 })
            {
                // Root level and Localization node level
                std::vector<FieldProcessInfo> v1CommonFields =
                {
                    { "PackageLocale", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Locale = value.as<std::string>(); return {}; } },
                    { "Publisher", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::Publisher>(value.as<std::string>()); return {}; } },
                    { "PublisherUrl", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::PublisherUrl>(value.as<std::string>()); return {}; } },
                    { "PublisherSupportUrl", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::PublisherSupportUrl>(value.as<std::string>()); return {}; } },
                    { "PrivacyUrl", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::PrivacyUrl>(value.as<std::string>()); return {}; } },
                    { "Author", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::Author>(value.as<std::string>()); return {}; } },
                    { "PackageName", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::PackageName>(Utility::Trim(value.as<std::string>())); return {}; } },
                    { "PackageUrl", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::PackageUrl>(value.as<std::string>()); return {}; } },
                    { "License", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::License>(value.as<std::string>()); return {}; } },
                    { "Copyright", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::Copyright>(value.as<std::string>()); return {}; } },
                    { "CopyrightUrl", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::CopyrightUrl>(value.as<std::string>()); return {}; } },
                    { "ShortDescription", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::ShortDescription>(Utility::Trim(value.as<std::string>())); return {}; } },
                    { "Tags", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::Tags>(ProcessStringSequenceNode(value)); return {}; } },
                };

                std::move(v1CommonFields.begin(), v1CommonFields.end(), std::inserter(result, result.end()));
            }

            if (manifestVersion >= ManifestVer{ s_ManifestVersionV1_1 })
            {
                std::vector<FieldProcessInfo> fields_v1_1 =
                {
                    { "Agreements", [this](const YAML::Node& value)->ValidationErrors { return ProcessAgreementsNode(value); }, true },
                    { "ReleaseNotes", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::ReleaseNotes>(value.as<std::string>()); return {}; } },
                    { "ReleaseNotesUrl", [this](const YAML::Node& value)->ValidationErrors { m_p_localization->Add<Localization::ReleaseNotesUrl>(value.as<std::string>()); return {}; } },
                };

                std::move(fields_v1_1.begin(), fields_v1_1.end(), std::inserter(result, result.end()));
            }
        }

        return result;
    }

    std::vector<ManifestYamlPopulator::FieldProcessInfo> ManifestYamlPopulator::GetDependenciesFieldProcessInfo(const ManifestVer& manifestVersion)
    {
        std::vector<FieldProcessInfo> result = {};

        if (manifestVersion >= ManifestVer{ s_ManifestVersionV1 })
        {
            result =
            {
                { "WindowsFeatures", [this](const YAML::Node& value)->ValidationErrors { ProcessDependenciesNode(DependencyType::WindowsFeature, value); return {}; } },
                { "WindowsLibraries", [this](const YAML::Node& value)->ValidationErrors { ProcessDependenciesNode(DependencyType::WindowsLibrary, value); return {}; } },
                { "PackageDependencies", [this](const YAML::Node& value)->ValidationErrors { ProcessPackageDependenciesNode(value); return {}; } },
                { "ExternalDependencies", [this](const YAML::Node& value)->ValidationErrors { ProcessDependenciesNode(DependencyType::External, value); return {}; } },
            };
        }

        return result;
    }

    void ManifestYamlPopulator::ProcessDependenciesNode(DependencyType type, const YAML::Node& node)
    {
        const auto& ids = ProcessStringSequenceNode(node);
        for (auto id : ids)
        {
            m_p_dependencyList->Add(Dependency(type, id));
        }
    }

    std::vector<ManifestYamlPopulator::FieldProcessInfo> ManifestYamlPopulator::GetPackageDependenciesFieldProcessInfo(const ManifestVer& manifestVersion)
    {
        std::vector<FieldProcessInfo> result = {};

        if (manifestVersion >= ManifestVer{ s_ManifestVersionV1 })
        {
            result =
            {
                { "PackageIdentifier", [this](const YAML::Node& value)->ValidationErrors { m_p_packageDependency->Id = Utility::Trim(value.as<std::string>()); return {}; } },
                { "MinimumVersion", [this](const YAML::Node& value)->ValidationErrors { m_p_packageDependency->MinVersion = Utility::Version(Utility::Trim(value.as<std::string>())); return {}; } },
            };
        }

        return result;
    }

    std::vector<ManifestYamlPopulator::FieldProcessInfo> ManifestYamlPopulator::GetAgreementFieldProcessInfo(const ManifestVer& manifestVersion)
    {
        std::vector<FieldProcessInfo> result = {};

        if (manifestVersion >= ManifestVer{ s_ManifestVersionV1_1 })
        {
            result =
            {
                { "AgreementLabel", [this](const YAML::Node& value)->ValidationErrors { m_p_agreement->Label = Utility::Trim(value.as<std::string>()); return {}; } },
                { "Agreement", [this](const YAML::Node& value)->ValidationErrors { m_p_agreement->AgreementText = Utility::Trim(value.as<std::string>()); return {}; } },
                { "AgreementUrl", [this](const YAML::Node& value)->ValidationErrors { m_p_agreement->AgreementUrl = Utility::Trim(value.as<std::string>()); return {}; } },
            };
        }

        return result;
    }

    std::vector<ManifestYamlPopulator::FieldProcessInfo> ManifestYamlPopulator::GetMarketsFieldProcessInfo(const ManifestVer& manifestVersion)
    {
        std::vector<FieldProcessInfo> result = {};

        if (manifestVersion >= ManifestVer{ s_ManifestVersionV1_1 })
        {
            result =
            {
                { "AllowedMarkets", [this](const YAML::Node& value)->ValidationErrors { m_p_markets->AllowedMarkets = ProcessStringSequenceNode(value); return {}; } },
                { "ExcludedMarkets", [this](const YAML::Node& value)->ValidationErrors { m_p_markets->ExcludedMarkets = ProcessStringSequenceNode(value); return {}; } },
            };
        }

        return result;
    }

    std::vector<ManifestYamlPopulator::FieldProcessInfo> ManifestYamlPopulator::GetAppsAndFeaturesEntryFieldProcessInfo(const ManifestVer& manifestVersion)
    {
        std::vector<FieldProcessInfo> result = {};

        if (manifestVersion >= ManifestVer{ s_ManifestVersionV1_1 })
        {
            result =
            {
                { "DisplayName", [this](const YAML::Node& value)->ValidationErrors { m_p_appsAndFeaturesEntry->DisplayName = Utility::Trim(value.as<std::string>()); return {}; } },
                { "Publisher", [this](const YAML::Node& value)->ValidationErrors { m_p_appsAndFeaturesEntry->Publisher = Utility::Trim(value.as<std::string>()); return {}; } },
                { "DisplayVersion", [this](const YAML::Node& value)->ValidationErrors { m_p_appsAndFeaturesEntry->DisplayVersion = Utility::Trim(value.as<std::string>()); return {}; } },
                { "ProductCode", [this](const YAML::Node& value)->ValidationErrors { m_p_appsAndFeaturesEntry->ProductCode = Utility::Trim(value.as<std::string>()); return {}; } },
                { "UpgradeCode", [this](const YAML::Node& value)->ValidationErrors { m_p_appsAndFeaturesEntry->UpgradeCode = Utility::Trim(value.as<std::string>()); return {}; } },
                { "InstallerType", [this](const YAML::Node& value)->ValidationErrors { m_p_appsAndFeaturesEntry->InstallerType = ConvertToInstallerTypeEnum(value.as<std::string>()); return {}; } },
            };
        }

        return result;
    }

    ValidationErrors ManifestYamlPopulator::ValidateAndProcessFields(
        const YAML::Node& rootNode,
        const std::vector<FieldProcessInfo>& fieldInfos)
    {
        ValidationErrors resultErrors;

        if (!rootNode.IsMap() || rootNode.size() == 0)
        {
            resultErrors.emplace_back(ManifestError::InvalidRootNode, "", "", m_isMergedManifest ? 0 : rootNode.Mark().line, m_isMergedManifest ? 0 : rootNode.Mark().column);
            return resultErrors;
        }

        // Keeps track of already processed fields. Used to check duplicate fields.
        std::set<std::string> processedFields;

        for (auto const& keyValuePair : rootNode.Mapping())
        {
            std::string key = keyValuePair.first.as<std::string>();
            const YAML::Node& valueNode = keyValuePair.second;

            // We'll do case insensitive search first and validate correct case later.
            auto fieldIter = std::find_if(fieldInfos.begin(), fieldInfos.end(),
                [&](auto const& s)
                {
                    return Utility::CaseInsensitiveEquals(s.Name, key);
                });

            if (fieldIter != fieldInfos.end())
            {
                const FieldProcessInfo& fieldInfo = *fieldIter;

                // Make sure the found key is in Pascal Case
                if (key != fieldInfo.Name)
                {
                    resultErrors.emplace_back(ManifestError::FieldIsNotPascalCase, key, "", m_isMergedManifest ? 0 : keyValuePair.first.Mark().line, m_isMergedManifest ? 0 : keyValuePair.first.Mark().column);
                }

                // Make sure it's not a duplicate key
                if (!processedFields.insert(fieldInfo.Name).second)
                {
                    resultErrors.emplace_back(ManifestError::FieldDuplicate, fieldInfo.Name, "", m_isMergedManifest ? 0 : keyValuePair.first.Mark().line, m_isMergedManifest ? 0 : keyValuePair.first.Mark().column);
                }

                if (fieldInfo.RequireVerifiedPublisher)
                {
                    resultErrors.emplace_back(ManifestError::FieldRequireVerifiedPublisher, fieldInfo.Name, "",
                        m_isMergedManifest ? 0 : keyValuePair.first.Mark().line, m_isMergedManifest ? 0 : keyValuePair.first.Mark().column,
                        m_validateOption.ErrorOnVerifiedPublisherFields ? ValidationError::Level::Error : ValidationError::Level::Warning);
                }

                if (!valueNode.IsNull())
                {
                    try
                    {
                        auto errors = fieldInfo.ProcessFunc(valueNode);
                        std::move(errors.begin(), errors.end(), std::inserter(resultErrors, resultErrors.end()));
                    }
                    catch (const std::exception&)
                    {
                        resultErrors.emplace_back(ManifestError::FieldFailedToProcess, fieldInfo.Name);
                    }
                }
            }
            else
            {
                // For full validation, also reports unrecognized fields as warning
                if (m_validateOption.FullValidation)
                {
                    resultErrors.emplace_back(ManifestError::FieldUnknown, key, "", m_isMergedManifest ? 0 : keyValuePair.first.Mark().line, m_isMergedManifest ? 0 : keyValuePair.first.Mark().column, ValidationError::Level::Warning);
                }
            }
        }

        return resultErrors;
    }

    ValidationErrors ManifestYamlPopulator::ProcessPackageDependenciesNode(const YAML::Node& rootNode)
    {
        ValidationErrors resultErrors;

        for (auto const& entry : rootNode.Sequence())
        {
            Dependency packageDependency = Dependency(DependencyType::Package);
            m_p_packageDependency = &packageDependency;
            auto errors = ValidateAndProcessFields(entry, PackageDependenciesFieldInfos);
            std::move(errors.begin(), errors.end(), std::inserter(resultErrors, resultErrors.end()));
            m_p_dependencyList->Add(std::move(packageDependency));
        }

        return resultErrors;
    }

    ValidationErrors ManifestYamlPopulator::ProcessAgreementsNode(const YAML::Node& agreementsNode)
    {
        THROW_HR_IF(E_INVALIDARG, !agreementsNode.IsSequence());

        ValidationErrors resultErrors;
        std::vector<Agreement> agreements;

        for (auto const& entry : agreementsNode.Sequence())
        {
            Agreement agreement;
            m_p_agreement = &agreement;
            auto errors = ValidateAndProcessFields(entry, AgreementFieldInfos);
            std::move(errors.begin(), errors.end(), std::inserter(resultErrors, resultErrors.end()));
            agreements.emplace_back(std::move(agreement));
        }

        if (!agreements.empty())
        {
            m_p_localization->Add<Localization::Agreements>(std::move(agreements));
        }

        return resultErrors;
    }

    std::vector<ValidationError> ManifestYamlPopulator::ProcessMarketsNode(const YAML::Node& marketsNode)
    {
        MarketsInfo markets;
        m_p_markets = &markets;
        auto errors = ValidateAndProcessFields(marketsNode, MarketsFieldInfos);
        m_p_installer->Markets = markets;
        return errors;
    }

    std::vector<ValidationError> ManifestYamlPopulator::ProcessAppsAndFeaturesEntriesNode(const YAML::Node& appsAndFeaturesEntriesNode)
    {
        THROW_HR_IF(E_INVALIDARG, !appsAndFeaturesEntriesNode.IsSequence());

        ValidationErrors resultErrors;
        std::vector<AppsAndFeaturesEntry> appsAndFeaturesEntries;

        for (auto const& entry : appsAndFeaturesEntriesNode.Sequence())
        {
            AppsAndFeaturesEntry appsAndFeaturesEntry;
            m_p_appsAndFeaturesEntry = &appsAndFeaturesEntry;
            auto errors = ValidateAndProcessFields(entry, AppsAndFeaturesEntryFieldInfos);
            std::move(errors.begin(), errors.end(), std::inserter(resultErrors, resultErrors.end()));
            appsAndFeaturesEntries.emplace_back(std::move(appsAndFeaturesEntry));
        }

        m_p_installer->AppsAndFeaturesEntries = appsAndFeaturesEntries;

        return resultErrors;
    }

    ValidationErrors ManifestYamlPopulator::ProcessExpectedReturnCodesNode(const YAML::Node& returnCodesNode)
    {
        THROW_HR_IF(E_INVALIDARG, !returnCodesNode.IsSequence());

        ValidationErrors resultErrors;
        std::map<DWORD, ExpectedReturnCodeEnum> returnCodes;

        for (auto const& entry : returnCodesNode.Sequence())
        {
            ExpectedReturnCode returnCode;
            m_p_expectedReturnCode = &returnCode;
            auto errors = ValidateAndProcessFields(entry, ExpectedReturnCodesFieldInfos);
            std::move(errors.begin(), errors.end(), std::inserter(resultErrors, resultErrors.end()));

            if (!returnCodes.insert({ returnCode.InstallerReturnCode, returnCode.ReturnResponse }).second)
            {
                resultErrors.emplace_back(ManifestError::DuplicateReturnCodeEntry);
            }
        }

        m_p_installer->ExpectedReturnCodes = returnCodes;

        return resultErrors;
    }

    ValidationErrors ManifestYamlPopulator::PopulateManifestInternal(
        const YAML::Node& rootNode,
        Manifest& manifest,
        const ManifestVer& manifestVersion,
        ManifestValidateOption validateOption)
    {
        m_validateOption = validateOption;
        m_isMergedManifest = !rootNode["ManifestType"sv].IsNull() && rootNode["ManifestType"sv].as<std::string>() == "merged";

        ValidationErrors resultErrors;
        manifest.ManifestVersion = manifestVersion;

        // Prepare field infos
        RootFieldInfos = GetRootFieldProcessInfo(manifestVersion);
        InstallerFieldInfos = GetInstallerFieldProcessInfo(manifestVersion);
        SwitchesFieldInfos = GetSwitchesFieldProcessInfo(manifestVersion);
        ExpectedReturnCodesFieldInfos = GetExpectedReturnCodesFieldProcessInfo(manifestVersion);
        DependenciesFieldInfos = GetDependenciesFieldProcessInfo(manifestVersion);
        PackageDependenciesFieldInfos = GetPackageDependenciesFieldProcessInfo(manifestVersion);
        LocalizationFieldInfos = GetLocalizationFieldProcessInfo(manifestVersion);
        AgreementFieldInfos = GetAgreementFieldProcessInfo(manifestVersion);
        MarketsFieldInfos = GetMarketsFieldProcessInfo(manifestVersion);
        AppsAndFeaturesEntryFieldInfos = GetAppsAndFeaturesEntryFieldProcessInfo(manifestVersion);

        // Populate root
        m_p_manifest = &manifest;
        m_p_installer = &(manifest.DefaultInstallerInfo);
        m_p_localization = &(manifest.DefaultLocalization);
        resultErrors = ValidateAndProcessFields(rootNode, RootFieldInfos);

        if (!m_p_installersNode)
        {
            return resultErrors;
        }

        // Populate installers
        for (auto const& entry : m_p_installersNode->Sequence())
        {
            ManifestInstaller installer = manifest.DefaultInstallerInfo;

            // Clear these defaults as PackageFamilyName, ProductCode, AppsAndFeaturesEntries need to be copied based on InstallerType
            installer.PackageFamilyName.clear();
            installer.ProductCode.clear();
            installer.AppsAndFeaturesEntries.clear();
            // Clear dependencies as installer overrides root dependencies
            installer.Dependencies.Clear();

            m_p_installer = &installer;
            auto errors = ValidateAndProcessFields(entry, InstallerFieldInfos);
            std::move(errors.begin(), errors.end(), std::inserter(resultErrors, resultErrors.end()));

            // Copy in system reference strings from the root if not set in the installer and appropriate
            if (installer.PackageFamilyName.empty() && DoesInstallerTypeUsePackageFamilyName(installer.InstallerType))
            {
                installer.PackageFamilyName = manifest.DefaultInstallerInfo.PackageFamilyName;
            }

            if (installer.ProductCode.empty() && DoesInstallerTypeUseProductCode(installer.InstallerType))
            {
                installer.ProductCode = manifest.DefaultInstallerInfo.ProductCode;
            }

            if (installer.AppsAndFeaturesEntries.empty() && DoesInstallerTypeWriteAppsAndFeaturesEntry(installer.InstallerType))
            {
                installer.AppsAndFeaturesEntries = manifest.DefaultInstallerInfo.AppsAndFeaturesEntries;
            }

            // If there are no dependencies on installer use default ones
            if (!installer.Dependencies.HasAny())
            {
                installer.Dependencies = manifest.DefaultInstallerInfo.Dependencies;
            }

            // Populate installer default switches if not exists
            auto defaultSwitches = GetDefaultKnownSwitches(installer.InstallerType);
            for (auto const& defaultSwitch : defaultSwitches)
            {
                if (installer.Switches.find(defaultSwitch.first) == installer.Switches.end())
                {
                    installer.Switches[defaultSwitch.first] = defaultSwitch.second;
                }
            }

            // Populate installer default return codes if not present in ExpectedReturnCodes and InstallerSuccessCodes
            auto defaultReturnCodes = GetDefaultKnownReturnCodes(installer.InstallerType);
            for (auto const& defaultReturnCode : defaultReturnCodes)
            {
                if (installer.ExpectedReturnCodes.find(defaultReturnCode.first) == installer.ExpectedReturnCodes.end() &&
                    std::find(installer.InstallerSuccessCodes.begin(), installer.InstallerSuccessCodes.end(), defaultReturnCode.first) == installer.InstallerSuccessCodes.end())
                {
                    installer.ExpectedReturnCodes[defaultReturnCode.first] = defaultReturnCode.second;
                }
            }

            manifest.Installers.emplace_back(std::move(installer));
        }

        // Populate additional localizations
        if (m_p_localizationsNode && m_p_localizationsNode->IsSequence())
        {
            for (auto const& entry : m_p_localizationsNode->Sequence())
            {
                ManifestLocalization localization;
                m_p_localization = &localization;
                auto errors = ValidateAndProcessFields(entry, LocalizationFieldInfos);
                std::move(errors.begin(), errors.end(), std::inserter(resultErrors, resultErrors.end()));
                manifest.Localizations.emplace_back(std::move(std::move(localization)));
            }
        }

        return resultErrors;
    }

    ValidationErrors ManifestYamlPopulator::PopulateManifest(
        const YAML::Node& rootNode,
        Manifest& manifest,
        const ManifestVer& manifestVersion,
        ManifestValidateOption validateOption)
    {
        ManifestYamlPopulator manifestPopulator;
        return manifestPopulator.PopulateManifestInternal(rootNode, manifest, manifestVersion, validateOption);
    }
}