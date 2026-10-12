//! Runtime CPU feature detection and selection of a safe implementation tier.
//!
//! Detection is based on ISA feature bits, never processor model names. The
//! detected set and default tier are each initialized once. Future ISA profiles
//! can add feature bits and tiers without changing the detection interface.

use std::sync::OnceLock;

/// Feature-bit mask for AVX-512 byte and word operations.
pub const FEATURE_AVX512BW: u32 = 1 << 0;
/// Feature-bit mask for AVX2.
pub const FEATURE_AVX2: u32 = 1 << 1;
/// Feature-bit mask for AArch64 Advanced SIMD (NEON).
pub const FEATURE_NEON: u32 = 1 << 2;

const ALL_FEATURES: u32 = FEATURE_AVX512BW | FEATURE_AVX2 | FEATURE_NEON;

static DETECTED_FEATURES: OnceLock<CpuFeatures> = OnceLock::new();
static DEFAULT_TIER: OnceLock<Tier> = OnceLock::new();

/// SIMD implementation tiers currently used by libobs Rust.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub enum Tier {
    /// x86 AVX-512BW; the feature-oriented API allows future AVX10 profiles.
    Avx512Bw,
    /// x86 AVX2 fallback.
    Avx2,
    /// AArch64 Advanced SIMD.
    Neon,
    /// Portable implementation with no optional ISA requirements.
    Scalar,
}

/// Runtime-detected optional CPU features.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct CpuFeatures {
    bits: u32,
}

impl CpuFeatures {
    /// Construct a feature set from a bit mask, ignoring unknown bits.
    ///
    /// This is useful for deterministic selection tests and for adding feature
    /// discovery on another platform without changing the selector.
    pub const fn from_bits(bits: u32) -> Self {
        Self {
            bits: bits & ALL_FEATURES,
        }
    }

    /// Return the optional features detected on this process's CPU.
    ///
    /// Rust's architecture macros include the platform's operating-system state
    /// checks for x86 vector registers, so selecting AVX code is safe only when
    /// both the CPU and OS can use it.
    pub fn detect() -> Self {
        let mut bits = 0;

        #[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
        {
            if std::arch::is_x86_feature_detected!("avx512bw") {
                bits |= FEATURE_AVX512BW;
            }
            if std::arch::is_x86_feature_detected!("avx2") {
                bits |= FEATURE_AVX2;
            }
        }

        #[cfg(target_arch = "aarch64")]
        {
            if std::arch::is_aarch64_feature_detected!("neon") {
                bits |= FEATURE_NEON;
            }
        }

        Self::from_bits(bits)
    }

    /// Return the once-cached runtime feature set.
    pub fn detected() -> Self {
        *DETECTED_FEATURES.get_or_init(Self::detect)
    }

    /// Return the raw feature mask.
    pub const fn bits(self) -> u32 {
        self.bits
    }

    /// Whether all bits in `feature` are present.
    pub const fn contains(self, feature: u32) -> bool {
        self.bits & feature == feature
    }

    /// Whether this host can safely execute the selected tier.
    pub const fn supports(self, tier: Tier) -> bool {
        match tier {
            Tier::Avx512Bw => self.contains(FEATURE_AVX512BW),
            Tier::Avx2 => self.contains(FEATURE_AVX2),
            Tier::Neon => self.contains(FEATURE_NEON),
            Tier::Scalar => true,
        }
    }

    /// Select the strongest available tier from this feature set.
    pub const fn best_tier(self) -> Tier {
        if self.contains(FEATURE_AVX512BW) {
            Tier::Avx512Bw
        } else if self.contains(FEATURE_AVX2) {
            Tier::Avx2
        } else if self.contains(FEATURE_NEON) {
            Tier::Neon
        } else {
            Tier::Scalar
        }
    }
}

/// Return the process's detected features, cached on first use.
pub fn features() -> CpuFeatures {
    CpuFeatures::detected()
}

/// Return the cached default implementation tier, unless a test override is
/// active on the current thread.
pub fn selected_tier() -> Tier {
    #[cfg(any(test, feature = "test-support"))]
    if let Some(tier) = FORCED_TIER.with(|slot| slot.get()) {
        return tier;
    }

    *DEFAULT_TIER.get_or_init(|| features().best_tier())
}

#[cfg(any(test, feature = "test-support"))]
std::thread_local! {
    static FORCED_TIER: std::cell::Cell<Option<Tier>> = const { std::cell::Cell::new(None) };
}

/// Error returned when a test asks to force a tier unsupported by this host.
#[cfg(any(test, feature = "test-support"))]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct UnsupportedTier(pub Tier);

/// Thread-scoped override guard. Dropping it restores the previous override.
#[cfg(any(test, feature = "test-support"))]
pub struct ForcedTierGuard {
    previous: Option<Tier>,
    _not_send: std::marker::PhantomData<*mut ()>,
}

#[cfg(any(test, feature = "test-support"))]
impl Drop for ForcedTierGuard {
    fn drop(&mut self) {
        FORCED_TIER.with(|slot| slot.set(self.previous));
    }
}

/// Force a tier for the current test thread if the host supports it.
///
/// The override is thread-local so parallel tests do not race. Unsupported
/// tiers are rejected to prevent a test from accidentally executing an
/// instruction the host cannot run. Enable the `test-support` Cargo feature to
/// use this from an integration test or another crate's tests.
#[cfg(any(test, feature = "test-support"))]
pub fn force_tier_for_test(tier: Tier) -> Result<ForcedTierGuard, UnsupportedTier> {
    if !features().supports(tier) {
        return Err(UnsupportedTier(tier));
    }

    let previous = FORCED_TIER.with(|slot| slot.replace(Some(tier)));
    Ok(ForcedTierGuard {
        previous,
        _not_send: std::marker::PhantomData,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn assert_detection_and_force(tier: Tier, detected: bool) {
        let host = features();
        assert_eq!(host.supports(tier), detected);

        if detected {
            let guard = force_tier_for_test(tier).expect("detected tier is supported");
            assert_eq!(selected_tier(), tier);
            drop(guard);
        }
    }

    #[test]
    fn best_tier_uses_feature_bits_in_priority_order() {
        assert_eq!(CpuFeatures::from_bits(0).best_tier(), Tier::Scalar);
        assert_eq!(CpuFeatures::from_bits(FEATURE_NEON).best_tier(), Tier::Neon);
        assert_eq!(CpuFeatures::from_bits(FEATURE_AVX2).best_tier(), Tier::Avx2);
        assert_eq!(
            CpuFeatures::from_bits(FEATURE_AVX512BW | FEATURE_AVX2).best_tier(),
            Tier::Avx512Bw
        );
    }

    #[test]
    fn tier_override_is_nested_and_thread_local() {
        let default = selected_tier();
        let host = features();
        let available = [Tier::Scalar, Tier::Avx2, Tier::Avx512Bw, Tier::Neon]
            .into_iter()
            .find(|&tier| host.supports(tier))
            .expect("scalar tier is always supported");

        let outer = force_tier_for_test(available).expect("selected tier is supported");
        assert_eq!(selected_tier(), available);

        let inner_tier = [Tier::Scalar, Tier::Avx2, Tier::Avx512Bw, Tier::Neon]
            .into_iter()
            .find(|&tier| host.supports(tier) && tier != available)
            .unwrap_or(available);
        let inner = force_tier_for_test(inner_tier).expect("selected tier is supported");
        assert_eq!(selected_tier(), inner_tier);
        drop(inner);
        assert_eq!(selected_tier(), available);

        let other_thread_tier = std::thread::spawn(selected_tier).join().unwrap();
        assert_eq!(other_thread_tier, default);
        drop(outer);
        assert_eq!(selected_tier(), default);
    }

    #[test]
    fn scalar_tier_can_be_forced() {
        let guard = force_tier_for_test(Tier::Scalar).expect("scalar tier is always supported");
        assert_eq!(selected_tier(), Tier::Scalar);
        drop(guard);
    }

    #[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
    #[test]
    fn avx512bw_detection_and_override_match_runtime_support() {
        assert_detection_and_force(
            Tier::Avx512Bw,
            std::arch::is_x86_feature_detected!("avx512bw"),
        );
    }

    #[cfg(any(target_arch = "x86", target_arch = "x86_64"))]
    #[test]
    fn avx2_detection_and_override_match_runtime_support() {
        assert_detection_and_force(Tier::Avx2, std::arch::is_x86_feature_detected!("avx2"));
    }

    #[cfg(target_arch = "aarch64")]
    #[test]
    fn neon_detection_and_override_match_runtime_support() {
        assert_detection_and_force(Tier::Neon, std::arch::is_aarch64_feature_detected!("neon"));
    }

    #[test]
    fn tier_override_rejects_unsupported_hardware() {
        let host = features();
        if let Some(tier) = [Tier::Avx512Bw, Tier::Avx2, Tier::Neon]
            .into_iter()
            .find(|&tier| !host.supports(tier))
        {
            assert!(matches!(
                force_tier_for_test(tier),
                Err(UnsupportedTier(unsupported)) if unsupported == tier
            ));
        }
    }
}
