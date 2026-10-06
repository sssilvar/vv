import { useState } from "react";
import type { Meta, StoryObj } from "@storybook/react-vite";
import { Lut, ScalarBar, type LutSliderProps } from "../src";

function Editable(args: LutSliderProps) {
  const [thresholds, setThresholds] = useState(args.thresholds);
  return <ScalarBar {...args} thresholds={thresholds} onChange={setThresholds} />;
}
const meta = {
  title: "ScalarBar",
  component: ScalarBar,
  args: { lut: Lut.rainbow(-80, 40), thresholds: [-80, 40] },
  argTypes: { lut: { control: false } },
  decorators: [
    (Story) => (
      <div style={{ height: 320, padding: 24 }}>
        <Story />
      </div>
    ),
  ],
} satisfies Meta<typeof ScalarBar>;
export default meta;
type Story = StoryObj<typeof meta>;

export const ReadOnly: Story = {};
export const Interactive: Story = {
  render: (args) => <Editable key={JSON.stringify(args.thresholds)} {...args} />,
};
export const Clipped: Story = { ...Interactive, args: { thresholds: [-50, 10] } };
export const Cyclic: Story = { ...Interactive, args: { lut: Lut.cyclic(-80, 40) } };
export const Categorical: Story = { args: { lut: Lut.categorical([2, 7, 42]), thresholds: [] } };
export const Constant: Story = { args: { lut: Lut.rainbow(5, 5), thresholds: [5, 5] } };
export const Segmented: Story = {
  ...Interactive,
  args: {
    lut: new Lut(
      [
        ["#0000ff", "#ffffff"],
        ["#ffffff", "#ff0000"],
      ],
      -80,
      40,
    ),
    thresholds: [-20],
  },
};
export const Units: Story = {
  ...Interactive,
  args: { valueFormatter: (value) => `${value.toFixed(1)} mV`, step: 1, dashHelpers: 4 },
};
export const HandlesOnly: Story = { ...Interactive, args: { editableThresholds: false } };
