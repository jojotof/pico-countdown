# PlatformIO Pre-Build Script (SCons)
Import("env")  # type: ignore
from datetime import datetime
import time

# Date cible
target_date = datetime(2026, 4, 3)
today = datetime.now()

# Calculer les jours restants
days_remaining = (target_date - today).days
if days_remaining < 0:
    days_remaining = 0
if days_remaining > 9999:
    days_remaining = 9999

# Générer un BUILD_ID unique basé sur le timestamp
build_id = int(time.time())

# Ajouter les flags de build
env.Append(CPPDEFINES=[  # type: ignore
    ("INIT_COUNTER", days_remaining),
    ("INIT_MAX_COUNTER", 60),
    ("BUILD_ID", build_id)
])

print(f"Counter will be initialized to: {days_remaining} days")
print(f"Max counter will be initialized to: 60 days")
print(f"Build ID: {build_id}")
