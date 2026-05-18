workspace "FusionDesk" "Enterprise remote desktop architecture baseline" {
    model {
        operator = person "Remote Operator" "Controls a remote desktop session."
        admin = person "Enterprise Admin" "Configures policy, licensing, and audit requirements."

        fusiondesk = softwareSystem "FusionDesk" "Enterprise remote desktop platform." {
            pcClient = container "PC Client" "Desktop client application." "Qt/C++"
            pcAgent = container "PC Agent" "Remote desktop agent." "Qt/C++"
            androidController = container "Android Controller Library" "Embeddable Android client controller." "AAR + JNI + Qt"
            runtime = container "FusionDesk Runtime" "Session orchestration, module composition, diagnostics." "C++"
            core = container "FusionDesk Core" "Pure C++ contracts for protocol, session, network, modules, policy." "C++17"
            modules = container "Feature Modules" "Display, input, audio, clipboard, filesystem, printer, camera." "C++"
            adapters = container "Adapters" "Qt, transport, codec, and platform adapters." "C++/Qt/OS"
        }

        auth = softwareSystem "Auth And Policy Service" "Login, feature authorization, enterprise policy."
        relay = softwareSystem "Relay Or P2P Tunnel" "Relay and future direct tunnel transport."
        audit = softwareSystem "Audit And Diagnostics Backend" "Enterprise audit and health event collection."

        operator -> pcClient "Uses"
        operator -> androidController "Uses embedded controller through Android app"
        admin -> auth "Configures"
        pcClient -> auth "Authenticates and receives policy"
        pcClient -> relay "Uses when direct connection is unavailable"
        relay -> pcAgent "Forwards traffic"
        pcClient -> pcAgent "Direct or relayed remote desktop traffic"
        pcClient -> audit "Publishes diagnostics"
        pcAgent -> audit "Publishes diagnostics and audit events"

        pcClient -> runtime "Creates RuntimeHost"
        pcAgent -> runtime "Creates RuntimeHost"
        androidController -> runtime "Creates client runtime through JNI"
        runtime -> core "Uses contracts"
        runtime -> modules "Composes and starts"
        modules -> core "Uses network, policy, protocol interfaces"
        modules -> adapters "Uses framework, transport, codec, or platform implementations"
        adapters -> core "Translates to core contracts"
    }

    views {
        systemContext fusiondesk "SystemContext" {
            include *
            autolayout lr
        }

        container fusiondesk "Containers" {
            include *
            autolayout tb
        }

        theme default
    }
}
