import type { CoolingDevice } from "@/types/telemetry";

export const mockDevices: CoolingDevice[] = [
  {
    id: "trc-001",
    sn: "TRC-240721-001",
    name: "Cooling Unit Gen 3",
    status: "online",
    lastReportSeconds: 4,
    locationLabel: "Los Angeles, CA",
  },
  {
    id: "trc-002",
    sn: "TRC-240721-002",
    name: "Cooling Unit Gen 3",
    status: "online",
    lastReportSeconds: 7,
    locationLabel: "Santa Monica, CA",
  },
  {
    id: "trc-003",
    sn: "TRC-240720-018",
    name: "Road Test Mule A",
    status: "delayed",
    lastReportSeconds: 36,
    locationLabel: "Glendale, CA",
  },
  {
    id: "trc-004",
    sn: "TRC-240718-011",
    name: "Cooling Unit Gen 2",
    status: "offline",
    lastReportSeconds: 428,
    locationLabel: "Pasadena, CA",
  },
  {
    id: "trc-005",
    sn: "TRC-240716-007",
    name: "Validation Rig B",
    status: "fault",
    lastReportSeconds: 8,
    locationLabel: "Long Beach, CA",
  },
  {
    id: "trc-006",
    sn: "TRC-240714-003",
    name: "Cooling Unit Gen 2",
    status: "online",
    lastReportSeconds: 5,
    locationLabel: "Anaheim, CA",
  },
  {
    id: "trc-007",
    sn: "TRC-240710-016",
    name: "Desert Test Vehicle",
    status: "online",
    lastReportSeconds: 9,
    locationLabel: "Palm Springs, CA",
  },
  {
    id: "trc-008",
    sn: "TRC-240706-021",
    name: "Cooling Unit Gen 2",
    status: "offline",
    lastReportSeconds: 1820,
    locationLabel: "Irvine, CA",
  },
];

export const totalDeviceCount = 18;
export const onlineDeviceCount = 13;
