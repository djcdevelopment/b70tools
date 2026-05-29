// app.jsx — mount the canvas + arrange the five surfaces.
const App = () => (
  <DesignCanvas>
    <DCSection id="approach" title="approach" subtitle="principles · screen index">
      <DCArtboard id="s0-title" label="S0 · approach" width={1200} height={760}>
        <TitleArtboard/>
      </DCArtboard>
    </DCSection>

    <DCSection id="session" title="S1 · session view" subtitle="primary live screen — adapters, arbitration, timeline, events">
      <DCArtboard id="s1-session" label="S1 · session view" width={1800} height={1080}>
        <SessionArtboard/>
      </DCArtboard>
    </DCSection>

    <DCSection id="adapter" title="S2 · adapter detail" subtitle="reconciled identity, provenance, memory, activity">
      <DCArtboard id="s2-adapter" label="S2 · adapter detail" width={1800} height={1080}>
        <AdapterArtboard/>
      </DCArtboard>
    </DCSection>

    <DCSection id="replay" title="S3 · replay & comparison" subtitle="scrub a recording · overlay a second with shared cursor">
      <DCArtboard id="s3-replay" label="S3 · replay & comparison" width={1800} height={1080}>
        <ReplayArtboard/>
      </DCArtboard>
    </DCSection>

    <DCSection id="cost" title="S4 · cost of observation" subtitle="self-metrics · collector budget · perturbation events">
      <DCArtboard id="s4-cost" label="S4 · cost of observation" width={1800} height={1080}>
        <CostArtboard/>
      </DCArtboard>
    </DCSection>
  </DesignCanvas>
);

ReactDOM.createRoot(document.getElementById("root")).render(<App/>);
