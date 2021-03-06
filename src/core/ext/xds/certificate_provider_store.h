//
//
// Copyright 2020 gRPC authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//

#ifndef GRPC_CORE_EXT_XDS_CERTIFICATE_PROVIDER_STORE_H
#define GRPC_CORE_EXT_XDS_CERTIFICATE_PROVIDER_STORE_H

#include <grpc/support/port_platform.h>

#include <map>

#include "absl/strings/string_view.h"

#include "src/core/ext/xds/certificate_provider_factory.h"
#include "src/core/lib/gprpp/ref_counted_ptr.h"
#include "src/core/lib/gprpp/sync.h"
#include "src/core/lib/security/certificate_provider.h"

namespace grpc_core {

// Map for xDS based grpc_tls_certificate_provider instances. The store should
// outlive the refs taken via `CreateOrGetCertificateProvider()`.
class CertificateProviderStore {
 public:
  struct PluginDefinition {
    std::string plugin_name;
    RefCountedPtr<CertificateProviderFactory::Config> config;
  };

  // Maps plugin instance (opaque) name to plugin defition.
  typedef std::map<std::string, PluginDefinition> PluginDefinitionMap;

  CertificateProviderStore(PluginDefinitionMap plugin_config_map)
      : plugin_config_map_(std::move(plugin_config_map)) {}

  // If a certificate provider corresponding to the instance name \a key is
  // found, a ref to the grpc_tls_certificate_provider is returned. If no
  // provider is found for the key, a new provider is created from the plugin
  // definition map.
  // Returns nullptr on failure to get or create a new certificate provider.
  RefCountedPtr<grpc_tls_certificate_provider> CreateOrGetCertificateProvider(
      absl::string_view key);

 private:
  // A thin wrapper around `grpc_tls_certificate_provider` which allows removing
  // the entry from the CertificateProviderStore when the refcount reaches zero.
  class CertificateProviderWrapper : public grpc_tls_certificate_provider {
   public:
    CertificateProviderWrapper(
        RefCountedPtr<grpc_tls_certificate_provider> certificate_provider,
        CertificateProviderStore* store, absl::string_view key)
        : certificate_provider_(std::move(certificate_provider)),
          store_(store),
          key_(key) {}

    ~CertificateProviderWrapper() override {
      store_->ReleaseCertificateProvider(key_, this);
    }

    grpc_core::RefCountedPtr<grpc_tls_certificate_distributor> distributor()
        const override {
      return certificate_provider_->distributor();
    }

    grpc_pollset_set* interested_parties() const override {
      return certificate_provider_->interested_parties();
    }

   private:
    RefCountedPtr<grpc_tls_certificate_provider> certificate_provider_;
    CertificateProviderStore* store_;
    absl::string_view key_;
  };

  RefCountedPtr<CertificateProviderWrapper> CreateCertificateProviderLocked(
      absl::string_view key);

  // Releases a previously created certificate provider from the certificate
  // provider map if the value matches \a wrapper.
  void ReleaseCertificateProvider(absl::string_view key,
                                  CertificateProviderWrapper* wrapper);

  Mutex mu_;
  // Map of plugin configurations
  PluginDefinitionMap plugin_config_map_;
  // Underlying map for the providers.
  std::map<absl::string_view, CertificateProviderWrapper*>
      certificate_providers_map_;
};

}  // namespace grpc_core

#endif  // GRPC_CORE_EXT_XDS_CERTIFICATE_PROVIDER_STORE_H
