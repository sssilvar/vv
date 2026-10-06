import type { Preview } from "@storybook/react-vite";

const preview: Preview = {
  parameters: { layout: "fullscreen", controls: { expanded: true } },
  decorators: [
    (Story) => (
      <main
        style={{
          padding: 16,
          background: "#17191d",
          color: "white",
          minHeight: "100vh",
          boxSizing: "border-box",
          fontFamily: "system-ui, sans-serif",
        }}
      >
        <Story />
      </main>
    ),
  ],
};

export default preview;
